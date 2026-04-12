# Android ARM64 Roadmap

Missing functionality for full Android parity with the Windows build. Items are roughly ordered by impact.

## ARM64 signature scanning

The biggest gap. On Windows, `FName::Init_Windows` and `Off::InSDK::ProcessEvent::InitPE_Windows` find engine functions by scanning executable sections for x86 instruction patterns (`LEA reg, [rip+disp32]`, `PUSH imm32`, `CALL rel32`, etc.) and resolving string references. None of this works on ARM64.

**What's needed:**

- **ARM64 instruction decoders** in a new `Arch_arm64.cpp` (or an `Architecture_arm64` namespace): resolve `ADRP+ADD`, `ADRP+LDR`, `B`/`BL` relative branches, `ADR` — the ARM64 equivalents of the x86 `Resolve32Bit*` family.
- **`FindByStringInAllSections` for ARM64**: scan executable segments for `ADRP+ADD` pairs whose target resolves to a known string address. This replaces the x86 `LEA`-based string reference finder.
- **`FName::Init_Android`**: use the ARM64 string-ref finder to locate `"ForwardShadingQuality_"` (or equivalent anchor string), then resolve the `BL` call to `FName::AppendString` from the surrounding code.
- **`Off::InSDK::ProcessEvent::InitPE_Android`**: walk the vtable of the first UObject, match `ProcessEvent` by testing `FunctionFlags` byte patterns (the `F7 xx` `TEST` patterns in `Offsets.cpp` are x86-specific; ARM64 uses `TST` with different encoding).
- **`NameArray::TryFindNameArray_Android` / `TryFindNamePool_Android`**: find `GNames` via `"ByteProperty"` string reference + surrounding `ADRP+LDR` to locate the global pointer, replacing the Windows `EnterCriticalSection` import-based search.

**Current workaround**: manual overrides in `Generator::InitEngineCore()`.

## INI config on Android

`Settings::Config::Load()` is a no-op on non-Windows (`GetPrivateProfileStringA` is Windows-only). A simple `Key=Value` parser for `Dumper-7.ini` would let users set `SleepTimeout`, `SDKNamespaceName`, and future Android-specific settings without recompiling.

## GWorld auto-detection

`Off::InSDK::World::InitGWorld()` scans all segments for a pointer matching a known `UWorld` instance. This works on Android but is slow and often fails to find GWorld because the pointer may live outside any `PT_LOAD` segment (e.g. in BSS or a GOT slot that `dl_iterate_phdr` doesn't surface). Could be improved by also scanning `/proc/self/maps` anonymous regions.

## `/proc/self/mem` access

`IsBadReadPtr` currently parses `/proc/self/maps` because `/proc/self/mem` returns `EACCES` for injected code in app process context. If the injection method changes (e.g. root-based `ptrace` attach, or Zygisk module), `/proc/self/mem` may become available and would be faster than maps parsing for single-address probes. The `SafeMemRead` / `ProcMemIsReadable` code path is implemented but currently dead; re-enable it when the fd open succeeds.

## `FProperty` support on Android

`bUseFProperty` detection works (it's reflection-based, not platform-specific), but the `FField` offset finder functions (`FindFFieldNextOffset`, `FindFFieldNameOffset`, `FindFFieldClassOffset`) haven't been validated on Android ARM64. UE 4.25+ games using `FProperty` may need additional OffsetFinder tuning.

## Generated SDK portability

The C++ SDK headers emitted by `CppGenerator` contain Windows-isms:
- `#include <Windows.h>` in `Basic.cpp`
- `GetModuleHandle(0)` in `GetImageBase()`
- `__thiscall` calling convention on 32-bit

These should be `#ifdef`-guarded or have Android equivalents so the generated SDK compiles on non-Windows targets.
