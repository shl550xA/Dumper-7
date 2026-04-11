# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Dumper-7 is an SDK generator for all Unreal Engine games (UE4 + UE5). It builds as a Windows DLL that is injected into a target game; on `DLL_PROCESS_ATTACH` it spawns a thread, walks the live engine's reflection data (`GObjects`, `FName`, `UStruct`, etc.), and writes generated SDK files to `Settings::Generator::SDKGenerationPath` (default `C:/Dumper-7`). Output formats: C++ headers, `.usmap` mappings, IDA mappings, and Dumpspace JSON.

## Build

Three supported build systems — all produce the same Windows x64 DLL. Always build **x64-Release** for use; Debug only when attaching a debugger to the target game to diagnose crashes.

- **MSBuild / Visual Studio**: open `Dumper-7.sln`, build `Dumper` project in `x64 | Release`.
- **CMake** (see [UsingCMake.md](UsingCMake.md)): `cmake --preset <preset>` then `cmake --build build --config Release`. Sources are picked up by `file(GLOB_RECURSE)` over `Dumper/*.cpp`, so new files require a reconfigure. Requires C++20.
- **xmake** (see [Xmake.md](Xmake.md)): driven by [xmake.lua](xmake.lua).

There is no test suite. Validation is done by injecting the resulting DLL into a game and inspecting the generated SDK / console output.

### Android ARM64 (compile-only stub)

The `android-arm64-{debug,release,prod}` presets in [CMakePresets.json](CMakePresets.json) build `libDumper-7.so` for `arm64-v8a` against NDK API level 28. Requires `ANDROID_NDK_ROOT`, `ANDROID_NDK_VERSION`, `ANDROID_SDK_ROOT`, and `ANDROID_CMAKE_VERSION` in the environment. **This build does not actually dump anything** — every Windows/x86 runtime entry point is routed through the stubs in [PlatformAndroid.h](Dumper/Platform/Private/PlatformAndroid.h) / [.cpp](Dumper/Platform/Private/PlatformAndroid.cpp) and [Arch_arm64.cpp](Dumper/Platform/Private/Arch_arm64.cpp). Its sole purpose is to let downstream Android projects link against the library while a real ARM64 implementation is in progress. See the `PLATFORM_WINDOWS` / `PLATFORM_ANDROID` guards in [Platform.h](Dumper/Platform/Public/Platform.h).

## Architecture

The pipeline runs from [Dumper/main.cpp](Dumper/main.cpp) `MainThread`:

1. `Settings::Config::Load()` — optionally overridden by a `Dumper-7.ini` next to the game exe or under `C:/Dumper-7` (see README).
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
- **Platform** ([Dumper/Platform/](Dumper/Platform/)) — Windows + x86 specifics: module/section scanning, pattern/string searches, ASM disassembly used by `OffsetFinder` (`Arch_x86.cpp`, `PlatformWindows.cpp`). `Public/Platform.h` and `Architecture.h` are the boundary the rest of the code uses.
- **Utils** ([Dumper/Utils/](Dumper/Utils/)) — Compression (oodle/zstd glue), Encoding, Json, Dumpspace helpers.
- **Settings** ([Dumper/Settings.h](Dumper/Settings.h), [Settings.cpp](Dumper/Settings.cpp)) — compile-time `constexpr` toggles plus the runtime `Settings::Config` namespace populated by `Dumper-7.ini`.

### Key invariant: `bUseFProperty`

Whether the target engine uses `UProperty` (UE ≤4.24-ish) or `FProperty` (UE 4.25+) is detected during `InitEngineCore` and stored in `Settings::Internal::bUseFProperty`. Most engine wrappers branch on this; when adding new property handling, follow the existing pattern rather than assuming one form.

## Modifying for a specific game

Per the README, the override surface lives in two places:

- **Offsets / runtime entry points**: `Generator::InitEngineCore()` in [Generator.cpp](Dumper/Generator/Private/Generators/Generator.cpp) — `ObjectArray::Init`, `InitObjectArrayDecryption`, `FName::Init`, `Off::InSDK::InitPE`.
- **GObjects layout**: ~line 30 of [ObjectArray.cpp](Dumper/Engine/Private/Unreal/ObjectArray.cpp) — add to `FFixedUObjectArrayLayouts` (UE4.11–4.20) or `FChunkedFixedUObjectArrayLayouts` (UE4.21+).

Only override when auto-detection fails — adding spurious overrides can break detection for other games.
