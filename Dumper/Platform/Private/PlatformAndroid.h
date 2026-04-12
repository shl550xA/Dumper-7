#pragma once

#include <cstdint>
#include <unistd.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <utility>

#include "Settings.h"

/*
 * Android port of the Platform interface. Implements the memory/module layer on
 * top of dl_iterate_phdr (shared object + PT_LOAD enumeration) and the Linux
 * PROCMAP_QUERY ioctl (VMA lookup, with a /proc/self/maps/msync fallback for
 * kernels older than 6.11). Architecture-level instruction parsing is not done
 * here - Architecture_x86_64:: stubs in Arch_arm64.cpp stay zero/false for the
 * x86 decoders, and Architecture_x86_64::IsValid64BitVirtualAddress enforces
 * the arm64 CONFIG_ARM64_VA_BITS=39 user-space upper bound.
 *
 * FName::Init / ProcessEvent::InitPE signature-scans are NOT done on Android;
 * the caller must supply manual overrides via FName::Init(offset, ...) and
 * Off::InSDK::ProcessEvent::InitPE(index) from Generator::InitEngineCore.
 */

// Opaque section/segment handle, matching Windows SectionInfo bit-for-bit so a
// single Platform.h dispatch works. A valid SectionInfo is obtained via
// PlatformAndroid::GetSectionInfo and consumed unchanged.
struct SectionInfo
{
private:
	uint8_t Data[0x10] = { 0x0 };

public:
	// A section info must be obtained from a platform-function
	SectionInfo() = delete;

public:
	inline bool IsValid() const
	{
		for (size_t i = 0; i < sizeof(Data); i++)
		{
			if (Data[i] != 0x0)
				return true;
		}

		return false;
	}
};

/*
 * Global-namespace shim for the Win32 Sleep(ms) API. Engine code calls this
 * outside any #ifdef PLATFORM_WINDOWS guard (ObjectArray.cpp, UnrealObjects.cpp,
 * Offsets.cpp), so we provide a real millisecond sleep via usleep.
 */
inline void Sleep(unsigned long Milliseconds)
{
	::usleep(static_cast<useconds_t>(Milliseconds) * 1000u);
}

// On Android the dumper runs as an injected library inside a host game process.
// Calling exit() would kill the game, so fatal errors abort only the dump thread.
#include <pthread.h>
[[noreturn]] inline void Dumper7FatalExit()
{
	::pthread_exit(nullptr);
	__builtin_unreachable();
}

// Forward declarations
namespace PlatformAndroid
{
	template<typename T>
	T* FinAlignedValueInRange(const T, const int32_t, uintptr_t, uint32_t);

	template<typename T>
	T* FindAlignedValueInSection(const SectionInfo&, T, const int32_t);

	template<typename T>
	T* FindAlignedValueInAllSections(const T Value, const int32_t Alignment = alignof(T), const uintptr_t StartAddress = 0x0, int32_t Range = 0x0, const char* const ModuleName = Settings::General::DefaultModuleName);
}

class AndroidPrivateImplHelper
{
public:
	template<typename T>
	friend T* PlatformAndroid::FinAlignedValueInRange(const T, const int32_t, uintptr_t, uint32_t);

	template<typename T>
	friend T* PlatformAndroid::FindAlignedValueInSection(const SectionInfo&, T, const int32_t);

	template<typename T>
	friend T* PlatformAndroid::FindAlignedValueInAllSections(const T, const int32_t, const uintptr_t, int32_t, const char* const);

private:
	using ValueCompareFuncType = bool(*)(const void* Value, const void* PotentialValueAddress);

private:
	static void* FinAlignedValueInRangeImpl(const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment, uintptr_t StartAddress, uint32_t Range);
	static void* FindAlignedValueInSectionImpl(const SectionInfo& Info, const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment);
	static void* FindAlignedValueInAllSectionsImpl(const void* ValuePtr, ValueCompareFuncType ComparisonFunction, const int32_t ValueTypeSize, const int32_t Alignment, const uintptr_t StartAddress, int32_t Range, const char* const ModuleName);
};

namespace PlatformAndroid
{
	consteval bool Is32Bit()
	{
		return false;
	}

	uintptr_t GetModuleBase(const char* const ModuleName = Settings::General::DefaultModuleName);
	uintptr_t GetOffset(const uintptr_t Address, const char* const ModuleName = Settings::General::DefaultModuleName);
	uintptr_t GetOffset(const void* Address, const char* const ModuleName = Settings::General::DefaultModuleName);

	SectionInfo GetSectionInfo(const std::string& SectionName, const char* const ModuleName = Settings::General::DefaultModuleName);
	void* IterateSectionWithCallback(const SectionInfo& Info, const std::function<bool(void* Address)>& Callback, uint32_t Granularity = 0x4, uint32_t OffsetFromEnd = 0x0);
	void* IterateAllSectionsWithCallback(const std::function<bool(void* Address)>& Callback, uint32_t Granularity = 0x4, uint32_t OffsetFromEnd = 0x0, const char* const ModuleName = Settings::General::DefaultModuleName);

	bool IsAddressInAnyModule(const uintptr_t Address);
	bool IsAddressInAnyModule(const void* Address);
	bool IsAddressInProcessRange(const uintptr_t Address);
	bool IsAddressInProcessRange(const void* Address);
	bool IsBadReadPtr(const uintptr_t Address);
	bool IsBadReadPtr(const void* Address);

	const void* GetAddressOfImportedFunction(const char* SearchModuleName, const char* ModuleToImportFrom, const char* SearchFunctionName);
	const void* GetAddressOfImportedFunctionFromAnyModule(const char* ModuleToImportFrom, const char* SearchFunctionName);

	const void* GetAddressOfExportedFunction(const char* SearchModuleName, const char* SearchFunctionName);

	template<bool bShouldResolve32BitJumps = true>
	std::pair<const void*, int32_t> IterateVTableFunctions(void** VTable, const std::function<bool(const uint8_t* Address, int32_t Index)>& CallBackForEachFunc, int32_t NumFunctions = 0x150, int32_t OffsetFromStart = 0x0);

	void* FindPattern(const char* Signature, const uint32_t Offset = 0, const bool bSearchAllSections = false, const uintptr_t StartAddress = 0x0, const char* const ModuleName = Settings::General::DefaultModuleName);
	void* FindPatternInRange(const char* Signature, const void* Start, const uintptr_t Range, const bool bRelative = false, const uint32_t Offset = 0);
	void* FindPatternInRange(const char* Signature, const uintptr_t Start, const uintptr_t Range, const bool bRelative = false, const uint32_t Offset = 0);
	void* FindPatternInRange(std::vector<int>&& Signature, const void* Start, const uintptr_t Range, const bool bRelative = false, uint32_t Offset = 0, const uint32_t SkipCount = 0);

	/*
	 * Stays no-op on Android: the Windows impl decodes x86 LEA/PUSH opcodes to
	 * resolve string references. ARM64 would need ADRP+ADD/LDR pair decoding,
	 * which is out of scope for this layer. Every current call site is already
	 * inside an #ifdef PLATFORM_WINDOWS block.
	 */
	template<bool bCheckIfLeaIsStrPtr = false, typename CharType = char>
	inline void* FindByStringInAllSections(const CharType* /*RefStr*/, const uintptr_t /*StartAddress*/ = 0x0, int32_t /*Range*/ = 0x0, const bool /*bSearchOnlyExecutableSections*/ = true, const char* const /*ModuleName*/ = Settings::General::DefaultModuleName)
	{
		return nullptr;
	}

	template<bool bCheckIfLeaIsStrPtr, typename CharType>
	inline void* FindStringInRange(const CharType* /*RefStr*/, const uintptr_t /*StartAddress*/, const int32_t /*Range*/)
	{
		return nullptr;
	}

	template<typename T>
	T* FinAlignedValueInRange(const T Value, const int32_t Alignment, uintptr_t StartAddress, uint32_t Range)
	{
		auto ComparisonFunction = [](const void* ValueAddr, const void* PotentialMatchAddr) -> bool
		{
			return *static_cast<const T*>(ValueAddr) == *static_cast<const T*>(PotentialMatchAddr);
		};

		return static_cast<T*>(AndroidPrivateImplHelper::FinAlignedValueInRangeImpl(&Value, ComparisonFunction, sizeof(Value), Alignment, StartAddress, Range));
	}

	template<typename T>
	T* FindAlignedValueInSection(const SectionInfo& Info, const T Value, const int32_t Alignment)
	{
		auto ComparisonFunction = [](const void* ValueAddr, const void* PotentialMatchAddr) -> bool
		{
			return *static_cast<const T*>(ValueAddr) == *static_cast<const T*>(PotentialMatchAddr);
		};

		return static_cast<T*>(AndroidPrivateImplHelper::FindAlignedValueInSectionImpl(Info, &Value, ComparisonFunction, sizeof(Value), Alignment));
	}

	/* See forward declaration above for default argument values */
	template<typename T>
	T* FindAlignedValueInAllSections(const T Value, const int32_t Alignment, const uintptr_t StartAddress, int32_t Range, const char* const ModuleName)
	{
		auto ComparisonFunction = [](const void* ValueAddr, const void* PotentialMatchAddr) -> bool
		{
			return *static_cast<const T*>(ValueAddr) == *static_cast<const T*>(PotentialMatchAddr);
		};

		return static_cast<T*>(AndroidPrivateImplHelper::FindAlignedValueInAllSectionsImpl(&Value, ComparisonFunction, sizeof(Value), Alignment, StartAddress, Range, ModuleName));
	}

	template<typename T>
	std::vector<T*> FindAllAlignedValuesInProcess(const T Value, const int32_t Alignment = alignof(T), const uintptr_t StartAddress = 0x0, int32_t Range = 0x0, const char* const ModuleName = Settings::General::DefaultModuleName)
	{
		std::vector<T*> Ret;

		uintptr_t LastFoundValueAddress = StartAddress;
		while (T* ValuePtr = FindAlignedValueInAllSections(Value, Alignment, LastFoundValueAddress, Range, ModuleName))
		{
			Ret.push_back(ValuePtr);
			LastFoundValueAddress = reinterpret_cast<uintptr_t>(ValuePtr) + sizeof(T);
			// Round up to alignment
			const uintptr_t AlignMask = static_cast<uintptr_t>(Alignment) - 1u;
			LastFoundValueAddress = (LastFoundValueAddress + AlignMask) & ~AlignMask;
		}

		return Ret;
	}
}
