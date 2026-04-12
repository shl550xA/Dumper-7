#include "PlatformAndroid.h"
#include "Arch_x86.h"
#include "TmpUtils.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <filesystem>
#include <link.h>
#include <mutex>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

// linux/fs.h defines PROCMAP_QUERY + struct procmap_query. NDK r29 ships this
// header; on older SDKs the ioctl is unavailable at runtime and we fall back
// to /proc/self/maps parsing (see IsBadReadPtr).
#include <linux/fs.h>

/*
 * Android port of the Platform interface. Source of truth for module layout is
 * dl_iterate_phdr, which gives us program headers for every loaded shared
 * object. The dumper consumes "sections" in Windows PE terminology; we map
 * those to PT_LOAD segments by permission (.text = PF_X, .data = PF_W, etc).
 *
 * Runtime notes:
 *   - PROCMAP_QUERY requires kernel 6.11+ (released 2024-09). Most shipping
 *     Android devices are below that; the ioctl path is a fast-path, and we
 *     fall back to /proc/self/maps parsing for the address validity checks
 *     on older kernels.
 *   - dl_iterate_phdr is available on every Android version and is the only
 *     supported way to walk loaded libraries without root.
 */

namespace
{
	using namespace PlatformAndroid;

	struct Segment
	{
		uintptr_t Start = 0;
		uintptr_t Size = 0;
		uint32_t Flags = 0; // PF_X | PF_W | PF_R
	};

	struct AndroidModule
	{
		std::string Name;           // basename, e.g. "libUE4.so"
		std::string Path;           // full dlpi_name (may be empty for main exe)
		uintptr_t Base = 0;
		uintptr_t End = 0;          // one past the last loaded byte
		std::vector<Segment> Segments;
	};

	// The public SectionInfo is 16 bytes. We pack the three useful fields into
	// it via std::bit_cast, matching the Windows WindowsSectionInfo pattern.
	struct AndroidSectionInfo
	{
		uintptr_t SegmentStart = 0;
		uint32_t SegmentSize = 0;
		uint32_t Flags = 0;

		inline bool IsValid() const { return SegmentStart != 0 && SegmentSize != 0; }
	};
	static_assert(sizeof(AndroidSectionInfo) == sizeof(SectionInfo),
		"AndroidSectionInfo must match SectionInfo size for bit_cast to round-trip.");
	static_assert(std::is_trivially_copyable_v<AndroidSectionInfo>);
	static_assert(std::is_trivially_copyable_v<SectionInfo>);

	inline SectionInfo PackSectionInfo(const AndroidSectionInfo& Info) { return std::bit_cast<SectionInfo>(Info); }
	inline AndroidSectionInfo UnpackSectionInfo(const SectionInfo& Info) { return std::bit_cast<AndroidSectionInfo>(Info); }
	inline SectionInfo InvalidSectionInfo() { return PackSectionInfo(AndroidSectionInfo{}); }

	/* ------------------------------------------------------------------ */
	/* Module cache                                                        */
	/* ------------------------------------------------------------------ */

	std::mutex g_ModulesMutex;
	std::vector<AndroidModule> g_Modules;
	bool g_ModulesInitialized = false;

	int DlIterateCallback(struct dl_phdr_info* Info, size_t /*Size*/, void* Data)
	{
		auto* Out = static_cast<std::vector<AndroidModule>*>(Data);

		AndroidModule Module;
		Module.Base = Info->dlpi_addr;
		Module.End = Info->dlpi_addr;

		const char* RawName = Info->dlpi_name ? Info->dlpi_name : "";
		Module.Path = RawName;
		if (RawName[0] == '\0')
		{
			Module.Name = "<main>";
		}
		else
		{
			Module.Name = std::filesystem::path(RawName).filename().string();
		}

		for (ElfW(Half) i = 0; i < Info->dlpi_phnum; ++i)
		{
			const ElfW(Phdr)& Ph = Info->dlpi_phdr[i];
			if (Ph.p_type != PT_LOAD)
				continue;

			Segment Seg;
			Seg.Start = Info->dlpi_addr + Ph.p_vaddr;
			Seg.Size = Ph.p_memsz;
			Seg.Flags = Ph.p_flags;
			Module.Segments.push_back(Seg);

			Module.End = std::max<uintptr_t>(Module.End, Seg.Start + Seg.Size);
		}

		if (!Module.Segments.empty())
			Out->push_back(std::move(Module));

		return 0; // continue iteration
	}

	// Rebuilds the module cache from dl_iterate_phdr. Safe to call repeatedly -
	// each call produces a fresh snapshot.
	void RefreshModulesLocked()
	{
		g_Modules.clear();
		dl_iterate_phdr(&DlIterateCallback, &g_Modules);
		g_ModulesInitialized = true;
	}

	const std::vector<AndroidModule>& GetModules()
	{
		std::lock_guard<std::mutex> Lock(g_ModulesMutex);
		if (!g_ModulesInitialized)
			RefreshModulesLocked();
		return g_Modules;
	}

	// Default module name candidates for UE4/UE5 Android builds, checked in
	// order when Settings::General::DefaultModuleName is nullptr. Real games
	// overwhelmingly use libUE4.so; the others cover UE5 and odd repackagers.
	constexpr std::array<const char*, 6> DefaultModuleCandidates = {
		"libUE4.so",
		"libUnreal.so",
		"libUE5.so",
		"libUnrealEngine.so",
		"libue4.so",
		"libue5.so",
	};

	const AndroidModule* FindModuleLocked(const char* ModuleName)
	{
		if (ModuleName == nullptr)
		{
			for (const char* Candidate : DefaultModuleCandidates)
			{
				for (const auto& Module : g_Modules)
				{
					if (strcasecmp(Module.Name.c_str(), Candidate) == 0)
						return &Module;
				}
			}
			// Fall back to the main executable if no UE library is loaded.
			if (!g_Modules.empty())
				return &g_Modules.front();
			return nullptr;
		}

		for (const auto& Module : g_Modules)
		{
			if (strcasecmp(Module.Name.c_str(), ModuleName) == 0)
				return &Module;
		}

		return nullptr;
	}

	// Returns a pointer to a cached module, refreshing the cache once on miss
	// (libraries loaded after first lookup are still visible).
	const AndroidModule* ResolveModule(const char* ModuleName)
	{
		std::lock_guard<std::mutex> Lock(g_ModulesMutex);
		if (!g_ModulesInitialized)
			RefreshModulesLocked();

		if (const AndroidModule* Hit = FindModuleLocked(ModuleName))
			return Hit;

		// Miss: refresh and retry once.
		RefreshModulesLocked();
		return FindModuleLocked(ModuleName);
	}

	const Segment* FindSegmentByFlags(const AndroidModule& Module, uint32_t Required, uint32_t Forbidden)
	{
		for (const auto& Seg : Module.Segments)
		{
			if ((Seg.Flags & Required) != Required)
				continue;
			if ((Seg.Flags & Forbidden) != 0)
				continue;
			return &Seg;
		}
		return nullptr;
	}

	/* ------------------------------------------------------------------ */
	/* VMA readability check                                               */
	/* ------------------------------------------------------------------ */

	inline uintptr_t PageAlignDown(uintptr_t Address)
	{
		const long PageSize = sysconf(_SC_PAGESIZE);
		const uintptr_t Mask = static_cast<uintptr_t>(PageSize > 0 ? PageSize : 4096) - 1u;
		return Address & ~Mask;
	}

	// PROCMAP_QUERY state: the ioctl may be unavailable (kernel < 6.11). We
	// probe once on first use and thereafter skip to the /proc/self/maps path.
	std::mutex g_ProcMapMutex;
	int g_ProcMapFd = -1;     // fd for /proc/self/maps (ioctl target)
	bool g_ProcMapProbed = false;
	bool g_ProcMapAvailable = false;

	void EnsureProcMapProbedLocked()
	{
		if (g_ProcMapProbed)
			return;

		g_ProcMapProbed = true;
		g_ProcMapFd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
		if (g_ProcMapFd < 0)
			return;

#ifdef PROCMAP_QUERY
		// Probe with a zero-initialized query; if the ioctl is implemented the
		// call returns either 0 or an EINVAL/ENOENT we interpret as "supported
		// but no VMA at query_addr". ENOTTY means the kernel doesn't know the
		// ioctl at all.
		struct procmap_query Q{};
		Q.size = sizeof(Q);
		Q.query_flags = PROCMAP_QUERY_COVERING_OR_NEXT_VMA;
		Q.query_addr = reinterpret_cast<__u64>(&Q); // any mapped user address
		const int Rc = ::ioctl(g_ProcMapFd, PROCMAP_QUERY, &Q);
		if (Rc == 0 || errno != ENOTTY)
			g_ProcMapAvailable = true;
#endif
	}

	// Queries whether a page containing `Address` is readable. Returns one of:
	//   1  -> readable
	//   0  -> not mapped / not readable
	//  -1  -> PROCMAP_QUERY unavailable (caller should use maps fallback)
	int ProcMapQueryReadable(uintptr_t Address)
	{
		std::lock_guard<std::mutex> Lock(g_ProcMapMutex);
		EnsureProcMapProbedLocked();

		if (!g_ProcMapAvailable || g_ProcMapFd < 0)
			return -1;

#ifdef PROCMAP_QUERY
		struct procmap_query Q{};
		Q.size = sizeof(Q);
		Q.query_flags = PROCMAP_QUERY_COVERING_OR_NEXT_VMA | PROCMAP_QUERY_VMA_READABLE;
		Q.query_addr = static_cast<__u64>(PageAlignDown(Address));

		if (::ioctl(g_ProcMapFd, PROCMAP_QUERY, &Q) != 0)
			return 0;

		const uintptr_t Page = PageAlignDown(Address);
		if (Q.vma_start <= Page && Page < Q.vma_end)
			return 1;
		return 0;
#else
		return -1;
#endif
	}

	/* ------------------------------------------------------------------ */
	/* /proc/self/maps based readability check. Parses the maps file into  */
	/* a sorted vector of VMAs and binary-searches for address containment.*/
	/* On miss, re-parses once (libraries may have been loaded since last  */
	/* parse). Works even when /proc/self/mem is EACCES (injected code).   */
	/* ------------------------------------------------------------------ */

	struct VMA
	{
		uintptr_t Start;
		uintptr_t End;
		bool Readable;
	};

	std::mutex g_VMAMutex;
	std::vector<VMA> g_VMAs;
	bool g_VMAsInitialized = false;

	void ParseProcSelfMaps()
	{
		g_VMAs.clear();

		FILE* Fp = ::fopen("/proc/self/maps", "r");
		if (!Fp)
			return;

		char Line[512];
		while (::fgets(Line, sizeof(Line), Fp))
		{
			uintptr_t Start = 0, End = 0;
			char Perms[5] = {};
			if (::sscanf(Line, "%lx-%lx %4s", &Start, &End, Perms) == 3)
				g_VMAs.push_back(VMA{ Start, End, Perms[0] == 'r' });
		}

		::fclose(Fp);

		// Already sorted by kernel, but ensure it for binary search
		std::sort(g_VMAs.begin(), g_VMAs.end(), [](const VMA& A, const VMA& B) { return A.Start < B.Start; });
	}

	// Returns: 1 = readable, 0 = mapped but not readable, -1 = not in any VMA
	int LookupVMA(uintptr_t Address)
	{
		// Binary search for the last VMA with Start <= Address
		auto It = std::upper_bound(g_VMAs.begin(), g_VMAs.end(), Address,
			[](uintptr_t Addr, const VMA& V) { return Addr < V.Start; });

		if (It != g_VMAs.begin())
		{
			--It;
			if (Address >= It->Start && Address < It->End)
				return It->Readable ? 1 : 0;
		}

		return -1;
	}

	bool MapsIsReadable(uintptr_t Address)
	{
		std::lock_guard<std::mutex> Lock(g_VMAMutex);

		if (!g_VMAsInitialized)
		{
			ParseProcSelfMaps();
			g_VMAsInitialized = true;
			std::cerr << std::format("Dumper-7: Parsed /proc/self/maps: {} VMAs\n", g_VMAs.size());
		}

		int Result = LookupVMA(Address);
		if (Result >= 0)
			return Result == 1;

		// Miss: re-parse once (new mappings may have appeared)
		ParseProcSelfMaps();
		Result = LookupVMA(Address);
		return Result == 1;
	}

	/* ------------------------------------------------------------------ */
	/* Aligned-size helper (copied verbatim from PlatformWindows.cpp)      */
	/* ------------------------------------------------------------------ */

	inline int64_t GetAlignedSizeWithOffsetFromEnd(const uint32_t SizeToAlign, const uint32_t Alignment, const uint32_t OffsetFromEnd)
	{
		const uint32_t ValueToAlign = (SizeToAlign - (Alignment - 1) - OffsetFromEnd);

		// There was an underflow in the above subtraction
		if (ValueToAlign > SizeToAlign)
			return -1;

		return Align(ValueToAlign, Alignment);
	}

	/* ------------------------------------------------------------------ */
	/* Pattern parsing (shared between FindPattern overloads)              */
	/* ------------------------------------------------------------------ */

	std::vector<int> PatternToBytes(const char* Pattern)
	{
		std::vector<int> Bytes;

		const auto Start = const_cast<char*>(Pattern);
		const auto End = const_cast<char*>(Pattern) + strlen(Pattern);

		for (auto Current = Start; Current < End; ++Current)
		{
			if (*Current == '?')
			{
				++Current;
				if (*Current == '?')
					++Current;
				Bytes.push_back(-1);
			}
			else
			{
				Bytes.push_back(static_cast<int>(strtoul(Current, &Current, 16)));
			}
		}

		return Bytes;
	}
} // anonymous namespace


/* ---------------------------------------------------------------------- */
/* AndroidPrivateImplHelper                                                */
/* ---------------------------------------------------------------------- */

void* AndroidPrivateImplHelper::FinAlignedValueInRangeImpl(const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment, uintptr_t StartAddress, uint32_t Range)
{
	const auto SizeFromEnd = GetAlignedSizeWithOffsetFromEnd(Range, Alignment, ValueTypeSize);

	if (SizeFromEnd == -1)
		return nullptr;

	for (int64_t i = 0x0; i <= SizeFromEnd; i += Alignment)
	{
		void* TypedPtr = reinterpret_cast<void*>(StartAddress + i);

		if (ComparisonFunction(ValuePtr, TypedPtr))
			return TypedPtr;
	}

	return nullptr;
}

void* AndroidPrivateImplHelper::FindAlignedValueInSectionImpl(const SectionInfo& Info, const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment)
{
	const AndroidSectionInfo Section = UnpackSectionInfo(Info);

	if (!Section.IsValid())
		return nullptr;

	return FinAlignedValueInRangeImpl(ValuePtr, ComparisonFunction, ValueTypeSize, Alignment, Section.SegmentStart, Section.SegmentSize);
}

void* AndroidPrivateImplHelper::FindAlignedValueInAllSectionsImpl(const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment, const uintptr_t StartAddress, int32_t Range, const char* const ModuleName)
{
	const AndroidModule* Module = ResolveModule(ModuleName);
	if (!Module)
		return nullptr;

	int32_t RemainingRange = Range;

	for (const auto& Seg : Module->Segments)
	{
		// Only scan readable segments.
		if ((Seg.Flags & PF_R) == 0)
			continue;

		uintptr_t SearchStart = Seg.Start;
		uint32_t SearchRange = static_cast<uint32_t>(Seg.Size);

		// Clip to [StartAddress, SegmentEnd) if a start address was given.
		if (StartAddress != 0)
		{
			if (StartAddress >= Seg.Start + Seg.Size)
				continue;
			if (StartAddress > Seg.Start)
			{
				SearchStart = StartAddress;
				SearchRange = static_cast<uint32_t>(Seg.Start + Seg.Size - StartAddress);
			}
		}

		if (RemainingRange > 0 && static_cast<int32_t>(SearchRange) > RemainingRange)
			SearchRange = static_cast<uint32_t>(RemainingRange);

		void* Hit = FinAlignedValueInRangeImpl(ValuePtr, ComparisonFunction, ValueTypeSize, Alignment, SearchStart, SearchRange);
		if (Hit)
			return Hit;

		if (RemainingRange > 0)
		{
			RemainingRange -= static_cast<int32_t>(SearchRange);
			if (RemainingRange <= 0)
				break;
		}
	}

	return nullptr;
}


/* ---------------------------------------------------------------------- */
/* PlatformAndroid - module / address basics                               */
/* ---------------------------------------------------------------------- */

uintptr_t PlatformAndroid::GetModuleBase(const char* const ModuleName)
{
	const AndroidModule* Module = ResolveModule(ModuleName);
	return Module ? Module->Base : 0;
}

uintptr_t PlatformAndroid::GetOffset(const uintptr_t Address, const char* const ModuleName)
{
	const uintptr_t Base = GetModuleBase(ModuleName);
	if (Base == 0 || Address < Base)
		return 0;
	return Address - Base;
}

uintptr_t PlatformAndroid::GetOffset(const void* Address, const char* const ModuleName)
{
	return GetOffset(reinterpret_cast<uintptr_t>(Address), ModuleName);
}

SectionInfo PlatformAndroid::GetSectionInfo(const std::string& SectionName, const char* const ModuleName)
{
	const AndroidModule* Module = ResolveModule(ModuleName);
	if (!Module)
		return InvalidSectionInfo();

	// ELF strips section headers at runtime, so the Windows section-name
	// vocabulary maps to PT_LOAD permissions: ".text"=executable,
	// ".data"/".bss"=writable-non-exec, ".rdata"/".rodata"=read-only.
	const Segment* Hit = nullptr;
	if (SectionName == ".text")
	{
		Hit = FindSegmentByFlags(*Module, PF_X | PF_R, 0);
	}
	else if (SectionName == ".data" || SectionName == ".bss")
	{
		Hit = FindSegmentByFlags(*Module, PF_W | PF_R, PF_X);
	}
	else if (SectionName == ".rdata" || SectionName == ".rodata")
	{
		Hit = FindSegmentByFlags(*Module, PF_R, PF_X | PF_W);
	}

	if (!Hit)
		return InvalidSectionInfo();

	AndroidSectionInfo Info;
	Info.SegmentStart = Hit->Start;
	Info.SegmentSize = static_cast<uint32_t>(Hit->Size);
	Info.Flags = Hit->Flags;
	return PackSectionInfo(Info);
}

void* PlatformAndroid::IterateSectionWithCallback(const SectionInfo& Info, const std::function<bool(void*)>& Callback, uint32_t Granularity, uint32_t OffsetFromEnd)
{
	const AndroidSectionInfo Section = UnpackSectionInfo(Info);
	if (!Section.IsValid() || !Callback)
		return nullptr;

	const int64_t IterationSize = GetAlignedSizeWithOffsetFromEnd(Section.SegmentSize, Granularity, OffsetFromEnd);
	if (IterationSize < 0)
		return nullptr;

	for (uintptr_t Current = Section.SegmentStart; Current < (Section.SegmentStart + IterationSize); Current += Granularity)
	{
		if (Callback(reinterpret_cast<void*>(Current)))
			return reinterpret_cast<void*>(Current);
	}

	return nullptr;
}

void* PlatformAndroid::IterateAllSectionsWithCallback(const std::function<bool(void*)>& Callback, uint32_t Granularity, uint32_t OffsetFromEnd, const char* const ModuleName)
{
	const AndroidModule* Module = ResolveModule(ModuleName);
	if (!Module || !Callback)
		return nullptr;

	for (const auto& Seg : Module->Segments)
	{
		if ((Seg.Flags & PF_R) == 0)
			continue;

		AndroidSectionInfo Info;
		Info.SegmentStart = Seg.Start;
		Info.SegmentSize = static_cast<uint32_t>(Seg.Size);
		Info.Flags = Seg.Flags;

		if (void* Hit = IterateSectionWithCallback(PackSectionInfo(Info), Callback, Granularity, OffsetFromEnd))
			return Hit;
	}

	return nullptr;
}

bool PlatformAndroid::IsAddressInAnyModule(const uintptr_t Address)
{
	const auto& Modules = GetModules();
	for (const auto& Module : Modules)
	{
		if (Address >= Module.Base && Address < Module.End)
			return true;
	}
	return false;
}

bool PlatformAndroid::IsAddressInAnyModule(const void* Address)
{
	return IsAddressInAnyModule(reinterpret_cast<uintptr_t>(Address));
}

bool PlatformAndroid::IsAddressInProcessRange(const uintptr_t Address)
{
	// Windows treats "in process range" as "within the main module or any
	// loaded DLL". On Android that's equivalent to "within some loaded .so"
	// - the default module (libUE4.so) is already covered by IsAddressInAnyModule.
	return IsAddressInAnyModule(Address);
}

bool PlatformAndroid::IsAddressInProcessRange(const void* Address)
{
	return IsAddressInProcessRange(reinterpret_cast<uintptr_t>(Address));
}

bool PlatformAndroid::IsBadReadPtr(const uintptr_t Address)
{
	if (!Architecture_x86_64::IsValid64BitVirtualAddress(Address))
		return true;

	const int QueryResult = ProcMapQueryReadable(Address);
	if (QueryResult == 1) return false;
	if (QueryResult == 0) return true;

	return !MapsIsReadable(Address);
}

bool PlatformAndroid::IsBadReadPtr(const void* Address)
{
	return IsBadReadPtr(reinterpret_cast<uintptr_t>(Address));
}

/* ---------------------------------------------------------------------- */
/* PlatformAndroid - symbol lookup                                         */
/* ---------------------------------------------------------------------- */

const void* PlatformAndroid::GetAddressOfImportedFunction(const char* /*SearchModuleName*/, const char* /*ModuleToImportFrom*/, const char* /*SearchFunctionName*/)
{
	// ELF PLT/GOT walking isn't implemented; every engine call site that
	// reaches this function is Windows-specific (NameArray.cpp searching for
	// kernel32!EnterCriticalSection). Returning nullptr matches the existing
	// behavior of the compile-only stub.
	return nullptr;
}

const void* PlatformAndroid::GetAddressOfImportedFunctionFromAnyModule(const char* /*ModuleToImportFrom*/, const char* /*SearchFunctionName*/)
{
	return nullptr;
}

const void* PlatformAndroid::GetAddressOfExportedFunction(const char* SearchModuleName, const char* SearchFunctionName)
{
	if (!SearchFunctionName)
		return nullptr;

	void* Handle = SearchModuleName
		? ::dlopen(SearchModuleName, RTLD_NOLOAD | RTLD_NOW)
		: ::dlopen(nullptr, RTLD_NOW);
	if (!Handle)
		return nullptr;

	void* Sym = ::dlsym(Handle, SearchFunctionName);
	::dlclose(Handle); // NOLOAD dlopen does not actually increment ref count in a way we care about here

	return Sym;
}

/* ---------------------------------------------------------------------- */
/* PlatformAndroid - pattern / vtable scanners                             */
/* ---------------------------------------------------------------------- */

template<bool bShouldResolve32BitJumps>
std::pair<const void*, int32_t> PlatformAndroid::IterateVTableFunctions(void** VTable, const std::function<bool(const uint8_t*, int32_t)>& CallBackForEachFunc, int32_t NumFunctions, int32_t OffsetFromStart)
{
	// Windows resolves 0xE9 jmp thunks here; ARM64 linker doesn't insert
	// equivalent thunks for cross-section calls, so we always pass the raw
	// function pointer through unchanged.
	if (!CallBackForEachFunc || !VTable)
		return { nullptr, -1 };

	const int32_t EffectiveCount = NumFunctions > 0 ? NumFunctions : 0x150;

	for (int32_t i = OffsetFromStart; i < EffectiveCount; i++)
	{
		const uintptr_t CurrentFuncAddress = reinterpret_cast<uintptr_t>(VTable[i]);

		if (CurrentFuncAddress == 0 || !IsAddressInProcessRange(CurrentFuncAddress))
			break;

		const uint8_t* ResolvedAddress = reinterpret_cast<const uint8_t*>(CurrentFuncAddress);

		if (CallBackForEachFunc(ResolvedAddress, i))
			return { ResolvedAddress, i };
	}

	return { nullptr, -1 };
}

void* PlatformAndroid::FindPattern(const char* Signature, const uint32_t Offset, const bool bSearchAllSections, const uintptr_t StartAddress, const char* const ModuleName)
{
	const AndroidModule* Module = ResolveModule(ModuleName);
	if (!Module)
		return nullptr;

	auto ScanSegment = [&](const Segment& Seg) -> void*
	{
		if ((Seg.Flags & PF_R) == 0)
			return nullptr;

		uintptr_t SearchStart = Seg.Start;
		uint32_t SearchRange = static_cast<uint32_t>(Seg.Size);

		if (StartAddress != 0)
		{
			if (StartAddress >= Seg.Start + Seg.Size)
				return nullptr;
			if (StartAddress > Seg.Start)
			{
				SearchStart = StartAddress;
				SearchRange = static_cast<uint32_t>(Seg.Start + Seg.Size - StartAddress);
			}
		}

		return FindPatternInRange(Signature, reinterpret_cast<const void*>(SearchStart), SearchRange, Offset != 0, Offset);
	};

	if (bSearchAllSections)
	{
		for (const auto& Seg : Module->Segments)
		{
			if (void* Hit = ScanSegment(Seg))
				return Hit;
		}
		return nullptr;
	}

	// Default: scan the first executable segment (== .text).
	if (const Segment* Text = FindSegmentByFlags(*Module, PF_X | PF_R, 0))
		return ScanSegment(*Text);

	return nullptr;
}

void* PlatformAndroid::FindPatternInRange(const char* Signature, const void* Start, const uintptr_t Range, const bool bRelative, const uint32_t Offset)
{
	return FindPatternInRange(PatternToBytes(Signature), Start, Range, bRelative, Offset);
}

void* PlatformAndroid::FindPatternInRange(const char* Signature, const uintptr_t Start, const uintptr_t Range, const bool bRelative, const uint32_t Offset)
{
	return FindPatternInRange(Signature, reinterpret_cast<const void*>(Start), Range, bRelative, Offset);
}

void* PlatformAndroid::FindPatternInRange(std::vector<int>&& Signature, const void* Start, const uintptr_t Range, const bool bRelative, uint32_t Offset, const uint32_t SkipCount)
{
	const auto PatternLength = static_cast<int64_t>(Signature.size());
	const auto* PatternBytes = Signature.data();

	for (int64_t i = 0; i < (static_cast<int64_t>(Range) - PatternLength); i++)
	{
		bool bFound = true;
		int CurrentSkips = 0;

		for (int64_t j = 0; j < PatternLength; ++j)
		{
			if (static_cast<const uint8_t*>(Start)[i + j] != PatternBytes[j] && PatternBytes[j] != -1)
			{
				bFound = false;
				break;
			}
		}
		if (bFound)
		{
			if (CurrentSkips != static_cast<int>(SkipCount))
			{
				CurrentSkips++;
				continue;
			}

			uintptr_t Address = reinterpret_cast<uintptr_t>(Start) + i;
			if (bRelative)
			{
				if (Offset == static_cast<uint32_t>(-1))
					Offset = static_cast<uint32_t>(PatternLength);

				// NOTE: RIP-relative resolution is x86-specific. On ARM64 these
				// bytes are meaningless; bRelative should stay false for any
				// pattern that actually runs on Android. Match the Windows
				// implementation so the code compiles and any x86-guarded
				// caller keeps its behavior.
				Address = ((Address + Offset + 4) + *reinterpret_cast<const int32_t*>(Address + Offset));
			}
			return reinterpret_cast<void*>(Address);
		}
	}

	return nullptr;
}


/* ---------------------------------------------------------------------- */
/* Explicit template instantiations                                        */
/* ---------------------------------------------------------------------- */

template std::pair<const void*, int32_t> PlatformAndroid::IterateVTableFunctions<true>(void**, const std::function<bool(const uint8_t*, int32_t)>&, int32_t, int32_t);
template std::pair<const void*, int32_t> PlatformAndroid::IterateVTableFunctions<false>(void**, const std::function<bool(const uint8_t*, int32_t)>&, int32_t, int32_t);
