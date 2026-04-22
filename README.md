
# Dumper-7

SDK Generator for all Unreal Engine games. Supported versions are all of UE4 and UE5.

Windows is the primary supported host. **Android ARM64 is also supported**:
the dumper builds as `libDumper-7.so`, injects into the game, and
produces a full C++ SDK that compiles on clang with
`SDK_SKIP_STATIC_ASSERTS=OFF` (every generated `static_assert` on struct
size / alignment / member offset passes).

### Tested games (Android ARM64)

| Package | UE | Dump | SDK compiles |
|---|---|---|---|
| `com.tencent.ig` (PUBG Mobile) | 4.18 | ok | ok |
| `com.tencent.tmgp.pubgmhd` (PUBGM CN) | 4.18 | ok | ok |
| `com.proxima.dfm` | 4.24 | ok | ok |
| `com.proximabeta.mf.uamo` | 4.26 | ok | fails — one intra-package enum name collision + one struct with an apparent game-side reflection bug (declared size disagrees with declared member size) |

All tested builds required per-game offset overrides in
[Generator::InitEngineCore()](Dumper/Generator/Private/Generators/Generator.cpp)
(see [Overriding Offsets](#overriding-offsets)); auto-discovery of
`FName::AppendString` / `ProcessEvent` on ARM64 is not yet implemented.

## How to use

### Windows

1. Build the DLL in `x64 | Release`:
   - **Visual Studio**: open `Dumper-7.sln` and build the `Dumper` project.
   - **CMake** (see [UsingCMake.md](UsingCMake.md)):
     `cmake --preset windows-release` then
     `cmake --build build --config Release`.
   - **xmake** (see [Xmake.md](Xmake.md)): `xmake build`.
2. Inject `Dumper-7.dll` into the target game.
3. The SDK is generated into the path specified by
   `Settings::SDKGenerationPath`, by default `C:\Dumper-7`.

### Android ARM64

Requires a rooted device and an injection tool such as
[AndKittyInjector](https://github.com/MJx0/AndKittyInjector). See the
[tested games](#tested-games-android-arm64) table above for
known-good packages.

1. Override the game-specific offsets in
   [Generator::InitEngineCore()](Dumper/Generator/Private/Generators/Generator.cpp) —
   on Android the auto-discovery of `FName::AppendString` /
   `ProcessEvent` currently fails (see [ROADMAP.md](ROADMAP.md) for
   why), so at minimum `FName::Init(...)` and
   `Off::InSDK::ProcessEvent::InitPE(...)` must be set for your game.
2. Set up the NDK environment:
   ```sh
   export ANDROID_NDK_ROOT=/path/to/android-ndk
   export ANDROID_NDK_VERSION=r29          # or whichever is installed
   export ANDROID_SDK_ROOT=/path/to/android-sdk
   export ANDROID_CMAKE_VERSION=3.28.1     # whichever is installed
   ```
3. Build `libDumper-7.so`:
   ```sh
   cmake --preset android-arm64-release
   cmake --build build/android-arm64-release -j
   ```
   Output: `build/android-arm64-release/lib/libDumper-7.so`.
4. Deploy and inject (the `-dl_memfd` flag lets AndKittyInjector load
   the library from an anonymous file descriptor, which avoids
   triggering SELinux denial on stock policy — no `setenforce 0`
   required):
   ```sh
   adb push build/android-arm64-release/lib/libDumper-7.so /data/local/tmp/
   # Launch the game, wait for libUE4.so to load, then:
   adb shell 'su -c "/data/local/tmp/AndKittyInjector -pkg <package> -lib /data/local/tmp/libDumper-7.so -dl_memfd"'
   ```
5. Watch progress: `adb logcat -s Dumper-7`.
6. The SDK is written under `/data/data/<package>/Dumper-7/`.
   Pull it to the host with the helper script
   [tools/pull_dump.sh](tools/pull_dump.sh) — pass the package as the
   first argument (e.g. `tools/pull_dump.sh com.tencent.tmgp.pubgmhd`).

- **See [UsingTheSDK](UsingTheSDK.md) for a guide to get started, or to migrate from an old SDK.**
## Support Me

KoFi: https://ko-fi.com/fischsalat \
Patreon: https://www.patreon.com/u119629245

LTC (LTC-network): `LLtXWxDbc5H9d96VJF36ZpwVX6DkYGpTJU` \
BTC (Bitcoin): `1DVDUMcotWzEG1tyd1FffyrYeu4YEh7spx` \
USDT (Tron (TRC20)): `TWHDoUr2H52Gb2WYdZe7z1Ct316gMg64ps`

## Overriding Offsets

- ### Only override any offsets if the generator doesn't find them, or if they are incorrect
- All overrides are made in **Generator::InitEngineCore()** inside of **Generator.cpp**

- GObjects (see [GObjects-Layout](#overriding-gobjects-layout) too)
  ```cpp
  ObjectArray::Init(/*GObjectsOffset*/, /*ChunkSize*/, /*bIsChunked*/);
  ```
  ```cpp
  /* Make sure only to use types which exist in the sdk (eg. uint8, uint64) */
  InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x8375); });
  ```
- FName::AppendString
  - Forcing GNames:
    ```cpp
    FName::Init(/*bForceGNames*/); // Useful if the AppendString offset is wrong
    ```
  - Overriding the offset:
    ```cpp
    FName::Init(/*OverrideOffset, OverrideType=[AppendString, ToString, GNames], bIsNamePool*/);
    ```
- ProcessEvent
  ```cpp
  Off::InSDK::InitPE(/*PEIndex*/);
  ```
## Overriding GObjects-Layout
- Only add a new layout if GObjects isn't automatically found for your game.
- Layout overrides are at roughly line 30 of `ObjectArray.cpp`
- For UE4.11 to UE4.20 add the layout to `FFixedUObjectArrayLayouts`
- For UE4.21 and higher add the layout to `FChunkedFixedUObjectArrayLayouts`
- **Examples:**
  ```cpp
  FFixedUObjectArrayLayout // Default UE4.11 - UE4.20
  {
      .ObjectsOffset = 0x0,
      .MaxObjectsOffset = 0x8,
      .NumObjectsOffset = 0xC
  }
  ```
  ```cpp
  FChunkedFixedUObjectArrayLayout // Default UE4.21 and above
  {
      .ObjectsOffset = 0x00,
      .MaxElementsOffset = 0x10,
      .NumElementsOffset = 0x14,
      .MaxChunksOffset = 0x18,
      .NumChunksOffset = 0x1C,
  }
  ```

## Config File
You can optionally dynamically change settings through a `Dumper-7.ini` file, instead of modifying `Settings.h`.
- **Per-game**: Create `Dumper-7.ini` in the same directory as the game's exe file.
- **Global**: Create `Dumper-7.ini` under `C:\Dumper-7`

Example:
```ini
[Settings]
SleepTimeout=100
SDKNamespaceName=MyOwnSDKNamespace
```
## Issues

If you have any issues using the Dumper, please create an Issue on this repository\
and explain the problem **in detail**.

- Should your game be crashing while dumping, attach Visual Studios' debugger to the game and inject the Dumper-7.dll in debug-configuration.
Then include screenshots of the exception causing the crash, a screenshot of the callstack, as well as the console output.

- Should there be any compiler-errors in the SDK please send screenshots of them. Please note that **only build errors** are considered errors, as Intellisense often reports false positives.
Make sure to always send screenshots of the code causing the first error, as it's likely to cause a chain-reaction of errors.

- Should your own dll-project crash, verify your code thoroughly to make sure the error actually lies within the generated SDK.
