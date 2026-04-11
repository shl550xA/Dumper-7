#include "PlatformAndroid.h"

/*
 * Android stub - every function returns a benign "nothing found" result. See PlatformAndroid.h.
 */

namespace PlatformAndroid
{
	uintptr_t GetModuleBase(const char* const /*ModuleName*/) { return 0; }
	uintptr_t GetOffset(const uintptr_t /*Address*/, const char* const /*ModuleName*/) { return 0; }
	uintptr_t GetOffset(const void* /*Address*/, const char* const /*ModuleName*/) { return 0; }

	SectionInfo GetSectionInfo(const std::string& /*SectionName*/, const char* const /*ModuleName*/) { return {}; }
	void* IterateSectionWithCallback(const SectionInfo& /*Info*/, const std::function<bool(void*)>& /*Callback*/, uint32_t /*Granularity*/, uint32_t /*OffsetFromEnd*/) { return nullptr; }
	void* IterateAllSectionsWithCallback(const std::function<bool(void*)>& /*Callback*/, uint32_t /*Granularity*/, uint32_t /*OffsetFromEnd*/, const char* const /*ModuleName*/) { return nullptr; }

	bool IsAddressInAnyModule(const uintptr_t /*Address*/) { return false; }
	bool IsAddressInAnyModule(const void* /*Address*/) { return false; }
	bool IsAddressInProcessRange(const uintptr_t /*Address*/) { return false; }
	bool IsAddressInProcessRange(const void* /*Address*/) { return false; }
	bool IsBadReadPtr(const uintptr_t /*Address*/) { return true; }
	bool IsBadReadPtr(const void* /*Address*/) { return true; }

	const void* GetAddressOfImportedFunction(const char* /*SearchModuleName*/, const char* /*ModuleToImportFrom*/, const char* /*SearchFunctionName*/) { return nullptr; }
	const void* GetAddressOfImportedFunctionFromAnyModule(const char* /*ModuleToImportFrom*/, const char* /*SearchFunctionName*/) { return nullptr; }

	const void* GetAddressOfExportedFunction(const char* /*SearchModuleName*/, const char* /*SearchFunctionName*/) { return nullptr; }

	void* FindPattern(const char* /*Signature*/, const uint32_t /*Offset*/, const bool /*bSearchAllSections*/, const uintptr_t /*StartAddress*/, const char* const /*ModuleName*/) { return nullptr; }
	void* FindPatternInRange(const char* /*Signature*/, const void* /*Start*/, const uintptr_t /*Range*/, const bool /*bRelative*/, const uint32_t /*Offset*/) { return nullptr; }
	void* FindPatternInRange(const char* /*Signature*/, const uintptr_t /*Start*/, const uintptr_t /*Range*/, const bool /*bRelative*/, const uint32_t /*Offset*/) { return nullptr; }
	void* FindPatternInRange(std::vector<int>&& /*Signature*/, const void* /*Start*/, const uintptr_t /*Range*/, const bool /*bRelative*/, uint32_t /*Offset*/, const uint32_t /*SkipCount*/) { return nullptr; }
}
