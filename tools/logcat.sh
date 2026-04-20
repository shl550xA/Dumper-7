#!/usr/bin/env bash
#
# Tail adb logcat filtered to the Dumper-7 tag. Clears the log buffer first
# so only output from this session shows up. Ctrl-C to stop.
#
# Usage: tools/logcat.sh

set -euo pipefail

command -v adb >/dev/null || { echo "adb not in PATH" >&2; exit 1; }
adb get-state >/dev/null 2>&1 || { echo "no device connected" >&2; exit 1; }

echo "[*] Clearing log buffer"
adb logcat -c

echo "[*] Tailing Dumper-7 (Ctrl-C to stop)"
exec adb logcat -s 'Dumper-7'