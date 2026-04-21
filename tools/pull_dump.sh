#!/usr/bin/env bash
#
# Compress the whole Dumper-7 output folder on-device (CppSDK, Dumpspace,
# Mappings, IDAMappings, OffsetExtractor, top-level object dumps, and
# anything else the dumper writes), pull it to host, and extract into
# SDKTest/. The output lives in the target app's private data dir, only
# readable by the app uid — so compression happens under `su`, then we
# stage the tarball in /data/local/tmp for adb pull.
#
# Usage: tools/pull_dump.sh [package]   (default: com.tencent.ig)

set -euo pipefail

PKG="${1:-com.tencent.ig}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_DIR="${PROJECT_ROOT}/SDKTest"
DEVICE_DIR="/data/user/0/${PKG}/Dumper-7"
STAGE="/data/local/tmp/dumper-dump.tar.gz"
LOCAL_TARBALL="${DEST_DIR}/dumper-dump.tar.gz"

command -v adb >/dev/null || { echo "adb not in PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no device connected" >&2; exit 1; }

echo "[*] Locating dump under ${DEVICE_DIR}"
# There's exactly one engine-version subdir per dump. Pick the newest.
ENGINE_DIR="$(adb shell "su -c 'ls -1t \"${DEVICE_DIR}\" 2>/dev/null | head -n1'" | tr -d '\r')"
if [[ -z "${ENGINE_DIR}" ]]; then
    echo "no dump found in ${DEVICE_DIR} — has the dumper run?" >&2
    exit 1
fi
ENGINE_PATH="${DEVICE_DIR}/${ENGINE_DIR}"
echo "[*] Engine dir: ${ENGINE_DIR}"

# tar from *inside* the engine dir so extracted paths don't carry the
# engine-version prefix (which contains '+' chars that confuse some tools).
echo "[*] Compressing on device -> ${STAGE}"
adb shell "su -c 'cd \"${ENGINE_PATH}\" && tar -czf \"${STAGE}\" . && chmod 644 \"${STAGE}\"'"

SIZE="$(adb shell "su -c 'stat -c %s \"${STAGE}\"'" | tr -d '\r')"
echo "[*] Tarball size: $(( SIZE / 1024 / 1024 )) MiB"

mkdir -p "${DEST_DIR}"
echo "[*] Pulling to ${LOCAL_TARBALL}"
adb pull "${STAGE}" "${LOCAL_TARBALL}" >/dev/null

echo "[*] Cleaning on-device tarball"
adb shell "su -c 'rm -f \"${STAGE}\"'"

# Wipe only the top-level entries the tarball is about to create, so
# non-output files under SDKTest/ (CMakeLists.txt, shim/, build/, etc.)
# stay put across runs. Tar entries are emitted as "./X/..." (because we
# archived "." above), so strip the leading "./" before picking the first
# path component. The previous "-F/" split made $1=="." for every entry
# and silently skipped the wipe — leaving stale files from pulls of a
# different package.
echo "[*] Extracting into ${DEST_DIR}"
mapfile -t TOP_LEVEL < <(
    tar -tzf "${LOCAL_TARBALL}" \
        | sed -n 's@^\(\./\)\{0,1\}\([^/]\+\)\(/.*\)\{0,1\}$@\2@p' \
        | sort -u
)
for item in "${TOP_LEVEL[@]}"; do
    [[ -z "${item}" || "${item}" == "." ]] && continue
    rm -rf "${DEST_DIR:?}/${item}"
done
tar -xzf "${LOCAL_TARBALL}" -C "${DEST_DIR}"
rm -f "${LOCAL_TARBALL}"

# Record which engine version this dump came from so CMake / humans can tell.
printf '%s\n' "${ENGINE_DIR}" > "${DEST_DIR}/.engine-version"

CPP_COUNT="$(find "${DEST_DIR}/CppSDK" -name '*.cpp' 2>/dev/null | wc -l)"
HPP_COUNT="$(find "${DEST_DIR}/CppSDK" \( -name '*.hpp' -o -name '*.inl' \) 2>/dev/null | wc -l)"
echo "[+] Done. ${CPP_COUNT} .cpp / ${HPP_COUNT} headers extracted to ${DEST_DIR}/"
