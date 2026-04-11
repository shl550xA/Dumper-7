#pragma once

#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <functional>
#include <utility>

#include "Settings.h"

/*
 * Global-namespace shims for Win32 symbols that engine code references outside
 * any #ifdef PLATFORM_WINDOWS guard. Minimal no-op replacements so the code
 * compiles on android-arm64.
 */
inline void Sleep(unsigned long /*Milliseconds*/) {}

/*
 * Android stub of the Platform interface.
 *
 * This file mirrors the PlatformWindows interface so that the generic Engine/Generator
 * code links on Android. None of these functions do anything useful at runtime - they
 * exist purely so the project compiles for android-arm64. See PlatformWindows.h for
 * the real implementation and the interface contract.
 */

struct SectionInfo
{
private:
	uint8_t Data[0x10] = { 0x0 };

public:
	SectionInfo() = default;

	inline bool IsValid() const { return false; }
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
	inline std::pair<const void*, int32_t> IterateVTableFunctions(void** /*VTable*/, const std::function<bool(const uint8_t* Address, int32_t Index)>& /*CallBackForEachFunc*/, int32_t /*NumFunctions*/ = 0x150, int32_t /*OffsetFromStart*/ = 0x0)
	{
		return { nullptr, -1 };
	}

	void* FindPattern(const char* Signature, const uint32_t Offset = 0, const bool bSearchAllSections = false, const uintptr_t StartAddress = 0x0, const char* const ModuleName = Settings::General::DefaultModuleName);
	void* FindPatternInRange(const char* Signature, const void* Start, const uintptr_t Range, const bool bRelative = false, const uint32_t Offset = 0);
	void* FindPatternInRange(const char* Signature, const uintptr_t Start, const uintptr_t Range, const bool bRelative = false, const uint32_t Offset = 0);
	void* FindPatternInRange(std::vector<int>&& Signature, const void* Start, const uintptr_t Range, const bool bRelative = false, uint32_t Offset = 0, const uint32_t SkipCount = 0);

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
	inline T* FinAlignedValueInRange(const T /*Value*/, const int32_t /*Alignment*/, uintptr_t /*StartAddress*/, uint32_t /*Range*/)
	{
		return nullptr;
	}

	template<typename T>
	inline T* FindAlignedValueInSection(const SectionInfo& /*Info*/, const T /*Value*/, const int32_t /*Alignment*/)
	{
		return nullptr;
	}

	template<typename T>
	inline T* FindAlignedValueInAllSections(const T /*Value*/, const int32_t /*Alignment*/ = alignof(T), const uintptr_t /*StartAddress*/ = 0x0, int32_t /*Range*/ = 0x0, const char* const /*ModuleName*/ = Settings::General::DefaultModuleName)
	{
		return nullptr;
	}

	template<typename T>
	inline std::vector<T*> FindAllAlignedValuesInProcess(const T /*Value*/, const int32_t /*Alignment*/ = alignof(T), const uintptr_t /*StartAddress*/ = 0x0, int32_t /*Range*/ = 0x0, const char* const /*ModuleName*/ = Settings::General::DefaultModuleName)
	{
		return {};
	}
}
