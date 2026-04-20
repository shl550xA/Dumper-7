#!/usr/bin/env bash
#
# Push libDumper-7.so to the device and inject it into the running game via
# AndKittyInjector. The `-dl_memfd` flag makes the injector load the library
# through an anonymous memfd_create descriptor, which sidesteps the SELinux
# exec/load denial on stock policy — no `setenforce 0` required.
#
# Prerequisites:
#   - Target game is running (libUE4.so loaded).
#   - AndKittyInjector binary is already at /data/local/tmp/AndKittyInjector
#     on the device.
#
# Usage: tools/push_and_inject.sh [package]   (default: com.tencent.ig)

set -euo pipefail

PKG="${1:-com.tencent.ig}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEVICE_LIB="/data/local/tmp/libDumper-7.so"
INJECTOR="/data/local/tmp/AndKittyInjector"

command -v adb >/dev/null || { echo "adb not in PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no device connected" >&2; exit 1; }

# Pick the first libDumper-7.so produced by a standard CMake preset.
# Prefer release; fall back to debug / prod.
SO_PATH=""
for dir in android-arm64-release android-arm64-debug android-arm64-prod; do
    candidate="${PROJECT_ROOT}/build/${dir}/lib/libDumper-7.so"
    if [[ -f "${candidate}" ]]; then
        SO_PATH="${candidate}"
        break
    fi
done
if [[ -z "${SO_PATH}" ]]; then
    echo "libDumper-7.so not found under ${PROJECT_ROOT}/build/android-arm64-*/lib/" >&2
    echo "build it first, e.g. cmake --build build/android-arm64-debug" >&2
    exit 1
fi

adb shell "[ -x '${INJECTOR}' ]" 2>/dev/null || {
    echo "injector not found at ${INJECTOR} (push AndKittyInjector there first)" >&2
    exit 1
}

echo "[*] Source lib: ${SO_PATH#${PROJECT_ROOT}/}"
echo "[*] Pushing to ${DEVICE_LIB}"
adb push "${SO_PATH}" "${DEVICE_LIB}" >/dev/null

echo "[*] Injecting into ${PKG}"
adb shell "su -c '${INJECTOR} -pkg \"${PKG}\" -lib \"${DEVICE_LIB}\" -dl_memfd'"

echo "[+] Done. Tail the log with: tools/logcat.sh"
