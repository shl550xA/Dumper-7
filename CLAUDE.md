# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Dumper-7 is an SDK generator for all Unreal Engine games (UE4 + UE5). On Windows it builds as a DLL injected into the target game; on Android it builds as a shared library (`libDumper-7.so`) injected via a tool like AndKittyInjector. On `DLL_PROCESS_ATTACH` (Windows) or `__attribute__((constructor))` (Android) it spawns a thread, walks the live engine's reflection data (`GObjects`, `FName`, `UStruct`, etc.), and writes generated SDK files. Output formats: C++ headers, `.usmap` mappings, IDA mappings, and Dumpspace JSON.

## Build

Three supported build systems. Always build **Release** for use; Debug only when attaching a debugger.

- **MSBuild / Visual Studio**: open `Dumper-7.sln`, build `Dumper` project in `x64 | Release`.
- **CMake** (see [UsingCMake.md](UsingCMake.md)): `cmake --preset <preset>` then `cmake --build build --config Release`. Sources are picked up by `file(GLOB_RECURSE)` over `Dumper/*.cpp`, so new files require a reconfigure. Requires C++20.
- **xmake** (see [Xmake.md](Xmake.md)): driven by [xmake.lua](xmake.lua).

There is no test suite. Validation is done by injecting the resulting DLL/SO into a game and inspecting the generated SDK / console output.

### Android ARM64

The `android-arm64-{debug,release,prod}` presets in [CMakePresets.json](CMakePresets.json) build `libDumper-7.so` for `arm64-v8a` against NDK API level 28. Requires `ANDROID_NDK_ROOT`, `ANDROID_NDK_VERSION`, `ANDROID_SDK_ROOT`, and `ANDROID_CMAKE_VERSION` in the environment.

**Verified target**: UE 4.18 / PUBG Mobile (`com.tencent.ig`). With the per-game manual offset overrides in `Generator::InitEngineCore()`, the dumper produces a full C++ SDK whose 800+ generated translation units compile cleanly on clang Itanium (NDK r29) — every emitted `static_assert` on struct size / alignment / member offset passes.

The platform layer is functional: module enumeration (`dl_iterate_phdr`), address validity (`/proc/self/maps` parsing + `PROCMAP_QUERY` fast path on kernel 6.11+), segment iteration, pattern scanning, and vtable walking all work.

**What requires manual overrides on Android**: auto-discovery of `FName::AppendString`, `GNames`/`NamePool`, and `ProcessEvent` relies on x86 signature scans (`LEA`/`PUSH` opcode decoding in `FindByStringInAllSections`) that don't apply to ARM64. Until ARM64 ADRP+ADD/LDR decoders are implemented, these must be supplied via `Generator::InitEngineCore()`. See [ROADMAP.md](ROADMAP.md) for details.

**SDK output path**: defaults to `/data/data/<package>/Dumper-7/` (derived from `__progname` at runtime). Logs go to `adb logcat -s Dumper-7`.

**Testing on device**: push `libDumper-7.so` to `/data/local/tmp/`, launch the game, wait for `libUE4.so` to load, inject with `AndKittyInjector -pkg <pkg> -lib /data/local/tmp/libDumper-7.so -dl_memfd` (`-dl_memfd` avoids needing `setenforce 0`). Helper scripts in [tools/](tools/): `push_and_inject.sh` (push+inject one-shot), `pull_dump.sh` (tar the on-device dump folder — CppSDK, Dumpspace, Mappings, etc. — and pull it to host), `logcat.sh` (filtered logs).

## Architecture

The pipeline runs from `RunDump()` in [Dumper/main.cpp](Dumper/main.cpp):

1. `Settings::Config::Load()` — optionally overridden by a `Dumper-7.ini` next to the game exe or under `C:/Dumper-7` (see README). INI parsing is Windows-only for now.
2. `Generator::InitEngineCore()` — bootstraps access to the live engine: locates `GObjects`, `FName::AppendString`/`GNames`, `ProcessEvent` index, and detects engine version + property layout (`UProperty` vs `FProperty`). This is the function to edit when overriding offsets for a stubborn game (see README "Overriding Offsets").
3. `Generator::InitInternal()` — runs `OffsetFinder` to discover member offsets on `UObject`/`UStruct`/`UFunction`/`UProperty`/etc.
4. `Generator::Generate<T>()` (in [Generator.h](Dumper/Generator/Public/Generators/Generator.h)) — templated over a `GeneratorImplementation` concept. The first call dumps `GObjects` to disk; each call then sets up output folders, registers per-generator predefined members, and invokes `T::Generate()`.

The four generator backends, all under [Dumper/Generator/Private/Generators/](Dumper/Generator/Private/Generators/):

- `CppGenerator` — the main C++ SDK headers.
- `MappingGenerator` — `.usmap` files.
- `IDAMappingGenerator` — IDA Pro symbol mappings.
- `DumpspaceGenerator` — Dumpspace JSON.

### Layered modules

- **Engine** ([Dumper/Engine/](Dumper/Engine/)) — runtime reflection access. `Public/Unreal/` defines the `UE*` wrapper types (`UEClass`, `UEFunction`, `UEProperty`, `FName`, `FString`, `ObjectArray`, `NameArray`) that abstract over UE4/UE5 + `UProperty`/`FProperty` differences. `Public/OffsetFinder/` discovers field offsets at runtime. `ObjectArray::Init` and `FName::Init` are the override entry points referenced from `Generator::InitEngineCore`.
- **Generator** ([Dumper/Generator/](Dumper/Generator/)) — analysis + emission. `Managers/` (`PackageManager`, `StructManager`, `EnumManager`, `MemberManager`, `DependencyManager`, `CollisionManager`) build a normalized model of every package, resolve cross-package dependencies, and rename colliding members. `Wrappers/` (`StructWrapper`, `EnumWrapper`, `MemberWrappers`) wrap the raw `UE*` engine types with the manager-resolved metadata that the generators consume. `PredefinedMembers.h` lets generators inject hand-written members/functions into specific UE classes.
- **Platform** ([Dumper/Platform/](Dumper/Platform/)) — platform + architecture abstraction. `Public/Platform.h` dispatches to `PlatformWindows` or `PlatformAndroid` via namespace alias. `Public/Architecture.h` exposes `Architecture_x86_64::` (real on x86, stub on ARM64 except `IsValid64BitVirtualAddress`). Windows uses PE section walking + `VirtualQuery`; Android uses `dl_iterate_phdr` + `/proc/self/maps` parsing + `PROCMAP_QUERY` ioctl.
- **Utils** ([Dumper/Utils/](Dumper/Utils/)) — Compression (oodle/zstd glue), Encoding, Json, Dumpspace helpers.
- **Settings** ([Dumper/Settings.h](Dumper/Settings.h), [Settings.cpp](Dumper/Settings.cpp)) — compile-time `constexpr` toggles plus the runtime `Settings::Config` namespace populated by `Dumper-7.ini`.

### Key invariant: `bUseFProperty`

Whether the target engine uses `UProperty` (UE ≤4.24-ish) or `FProperty` (UE 4.25+) is detected during `InitEngineCore` and stored in `Settings::Internal::bUseFProperty`. Most engine wrappers branch on this; when adding new property handling, follow the existing pattern rather than assuming one form.

### Key invariant: `TCHAR` (dumper-internal) and `DUMPER7_TCHAR` (emitted)

UE4/UE5 uses UTF-16 internally for `TCHAR`/`FString` on all platforms. On Windows `wchar_t` is 16-bit and matches directly; on Android `wchar_t` is 32-bit, so the dumper-side `TCHAR` typedef is aliased to `char16_t`. See [Enums.h](Dumper/Engine/Public/Unreal/Enums.h) for the typedef and `TCHARToWString`/`TCHARLen`/`TCHARCmp` helpers. `FString` (dumper-side, in [UnrealContainers.h](Dumper/Engine/Public/Unreal/UnrealContainers.h)) extends `TArray<TCHAR>`, not `TArray<wchar_t>`.

The *emitted* SDK has its own abstraction: `DUMPER7_TCHAR` + `DUMPER7_TEXT(x)` (prefixed to avoid collision with Windows' `<tchar.h>`). The emitter defines them in the generated `UnrealContainers.hpp` so a single generated SDK compiles on both MSVC and clang Itanium without runtime narrowing. See [CppGenerator.cpp:GenerateUnrealContainers](Dumper/Generator/Private/Generators/CppGenerator.cpp).

## Modifying for a specific game

Per the README, the override surface lives in two places:

- **Offsets / runtime entry points**: `Generator::InitEngineCore()` in [Generator.cpp](Dumper/Generator/Private/Generators/Generator.cpp) — `ObjectArray::Init`, `InitObjectArrayDecryption`, `FName::Init`, `Off::InSDK::InitPE`.
- **GObjects layout**: ~line 30 of [ObjectArray.cpp](Dumper/Engine/Private/Unreal/ObjectArray.cpp) — add to `FFixedUObjectArrayLayouts` (UE4.11–4.20) or `FChunkedFixedUObjectArrayLayouts` (UE4.21+).

Only override when auto-detection fails — adding spurious overrides can break detection for other games.

On Android, auto-detection of FName/ProcessEvent always fails (no ARM64 signature scans yet), so overrides are mandatory. GObjects auto-scan works but is slow (~6s); providing the offset directly is faster.
