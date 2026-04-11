#include "Arch_x86.h"

/*
 * ARM64 stubs for the Architecture_x86_64 namespace. These exist so the generic
 * Engine code (which directly calls Architecture_x86_64::) links on android-arm64.
 * None of the helpers do anything meaningful on ARM - they all return NULL/false.
 */

bool Architecture_x86_64::IsValid64BitVirtualAddress(const uintptr_t /*Address*/) { return false; }
bool Architecture_x86_64::IsValid64BitVirtualAddress(const void* /*Address*/) { return false; }

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
