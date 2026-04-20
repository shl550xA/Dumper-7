#!/usr/bin/env bash
#
# Compress the Dumper-7 CppSDK on-device, pull it to host, and extract it into
# SDKTest/. The SDK is written by the dumper into the target app's private
# data dir, which is only readable by the app uid — so compression happens
# under `su`, then we stage the tarball in /data/local/tmp for adb pull.
#
# Usage: tools/pull_sdk.sh [package]   (default: com.tencent.ig)

set -euo pipefail

PKG="${1:-com.tencent.ig}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_DIR="${PROJECT_ROOT}/SDKTest"
DEVICE_DIR="/data/user/0/${PKG}/Dumper-7"
STAGE="/data/local/tmp/dumper-sdk.tar.gz"
LOCAL_TARBALL="${DEST_DIR}/dumper-sdk.tar.gz"

command -v adb >/dev/null || { echo "adb not in PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no device connected" >&2; exit 1; }

echo "[*] Locating SDK under ${DEVICE_DIR}"
# There's exactly one engine-version subdir per dump. Pick the newest.
ENGINE_DIR="$(adb shell "su -c 'ls -1t \"${DEVICE_DIR}\" 2>/dev/null | head -n1'" | tr -d '\r')"
if [[ -z "${ENGINE_DIR}" ]]; then
    echo "no SDK found in ${DEVICE_DIR} — has the dumper run?" >&2
    exit 1
fi
SDK_DIR="${DEVICE_DIR}/${ENGINE_DIR}/CppSDK"
echo "[*] Engine dir: ${ENGINE_DIR}"

# tar from *inside* the CppSDK dir so extracted paths don't carry the
# engine-version prefix (which contains '+' chars that confuse some tools).
echo "[*] Compressing on device -> ${STAGE}"
adb shell "su -c 'cd \"${SDK_DIR}\" && tar -czf \"${STAGE}\" . && chmod 644 \"${STAGE}\"'"

SIZE="$(adb shell "su -c 'stat -c %s \"${STAGE}\"'" | tr -d '\r')"
echo "[*] Tarball size: $(( SIZE / 1024 / 1024 )) MiB"

mkdir -p "${DEST_DIR}/CppSDK"
echo "[*] Pulling to ${LOCAL_TARBALL}"
adb pull "${STAGE}" "${LOCAL_TARBALL}" >/dev/null

echo "[*] Cleaning on-device tarball"
adb shell "su -c 'rm -f \"${STAGE}\"'"

echo "[*] Extracting into ${DEST_DIR}/CppSDK"
rm -rf "${DEST_DIR}/CppSDK"
mkdir -p "${DEST_DIR}/CppSDK"
tar -xzf "${LOCAL_TARBALL}" -C "${DEST_DIR}/CppSDK"
rm -f "${LOCAL_TARBALL}"

# Record which engine version this SDK came from so CMake / humans can tell.
printf '%s\n' "${ENGINE_DIR}" > "${DEST_DIR}/CppSDK/.engine-version"

CPP_COUNT="$(find "${DEST_DIR}/CppSDK" -name '*.cpp' | wc -l)"
HPP_COUNT="$(find "${DEST_DIR}/CppSDK" -name '*.hpp' -o -name '*.inl' | wc -l)"
echo "[+] Done. ${CPP_COUNT} .cpp / ${HPP_COUNT} headers extracted to ${DEST_DIR}/CppSDK"
