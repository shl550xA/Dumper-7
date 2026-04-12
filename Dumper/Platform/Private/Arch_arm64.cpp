#include "Arch_x86.h"
#include <cstdint>

/*
 * ARM64 stubs for the Architecture_x86_64 namespace. These exist so the generic
 * Engine code (which directly calls Architecture_x86_64::) links on android-arm64.
 * None of the helpers do anything meaningful on ARM - they all return NULL/false.
 */

bool Architecture_x86_64::IsValid64BitVirtualAddress(const uintptr_t Address)
{
	// PAC'ed pointers arent supported

	// support malloc'ed pointers?,
	// https://cs.android.com/android/platform/superproject/+/android-16.0.0_r1:bionic/libc/bionic/malloc_tagged_pointers.h;l=50
	constexpr uintptr_t BionicMemoryTag = 0xB4;
	auto without_tag = [](uintptr_t A) {
		// strip ONLY tag bits so < check below will fail for invalid tag
		return A & ~(BionicMemoryTag << 56);
	};

	// CONFIG_ARM64_VA_BITS = 39,
	// https://cs.android.com/android/kernel/superproject/+/common-android-mainline:common/include/asm-generic/access_ok.h;l=31
	constexpr uintptr_t UserMaxAddress = (uintptr_t)1 << 39;
	return Address != 0 && without_tag(Address) < UserMaxAddress;
}

bool Architecture_x86_64::IsValid64BitVirtualAddress(const void* Address)
{
	return IsValid64BitVirtualAddress(reinterpret_cast<uintptr_t>(Address));
}

bool Architecture_x86_64::Is32BitRIPRelativeJump(const uintptr_t /*Address*/) { return false; }

uintptr_t Architecture_x86_64::Resolve32BitRIPRelativeJumpTarget(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitRegisterRelativeJump(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitSectionRelativeCall(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitRelativeCall(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitRelativeMove(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitRelativeLea(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32BitRelativePush(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32bitAbsoluteCall(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::Resolve32bitAbsoluteMove(const uintptr_t /*Address*/) { return 0; }

bool Architecture_x86_64::IsFunctionRet(const uintptr_t /*Address*/) { return false; }
uintptr_t Architecture_x86_64::ResolveJumpIfInstructionIsJump(const uintptr_t /*Address*/, const uintptr_t DefaultReturnValueOnFail) { return DefaultReturnValueOnFail; }

uintptr_t Architecture_x86_64::FindNextFunctionStart(const uintptr_t /*Address*/) { return 0; }
uintptr_t Architecture_x86_64::FindNextFunctionStart(const void* /*Address*/) { return 0; }

uintptr_t Architecture_x86_64::FindFunctionEnd(const uintptr_t /*Address*/, uint32_t /*Range*/) { return 0; }

uintptr_t Architecture_x86_64::GetRipRelativeCalledFunction(const uintptr_t /*Address*/, const int32_t /*OneBasedFuncIndex*/, bool(* /*IsWantedTarget*/)(const uintptr_t)) { return 0; }
