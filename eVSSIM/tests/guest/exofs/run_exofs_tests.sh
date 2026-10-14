#!/bin/bash
# Guest side runner of the exofs test suite (runs as root inside the
# Ubuntu 14.04 / kernel 7.0.2 VM).
#
#   sudo ./run_exofs_tests.sh                    # whole suite
#   sudo ./run_exofs_tests.sh test_02_exofs_api.py:TestRename
#   sudo EXOFS_STRICT=1 ./run_exofs_tests.sh     # include known-issue tests
#
# Output (OUTPUT_DIR, default /tmp/exofs-results): exofs_results.xml (xunit),
# nosetests.log, dmesg-before.txt, dmesg-after.txt, environment.txt
set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-/tmp/exofs-results}"
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR run as root (sudo)"
    exit 1
fi

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

{
    echo "date: $(date)"
    echo "kernel: $(uname -a)"
    echo "distro: $(lsb_release -ds 2>/dev/null)"
    echo "python: $(python --version 2>&1)"
    echo "nose: $(nosetests --version 2>&1)"
    echo "nvme: $(command -v nvme) $(nvme version 2>&1)"
    echo "mkfs.exofs: $(command -v mkfs.exofs)"
    echo "exofs.ko: $(ls -l /lib/modules/$(uname -r)/kernel/fs/exofs/exofs.ko 2>&1)"
    echo "drives:"; ls -l /dev/nvme* 2>&1
    env | grep -E '^(EXOFS|EVSSIM)_' | sort
} > "$OUTPUT_DIR/environment.txt"
cat "$OUTPUT_DIR/environment.txt"

dmesg > "$OUTPUT_DIR/dmesg-before.txt"

cd "$TEST_DIR" || exit 1
if [ $# -gt 0 ]; then
    tests=("$@")
else
    tests=(test_01_exofs_bringup.py test_02_exofs_api.py test_99_exofs_dmesg.py)
fi

python -m nose -v -s --with-xunit --xunit-file="$OUTPUT_DIR/exofs_results.xml" "${tests[@]}" 2>&1 \
    | tee "$OUTPUT_DIR/nosetests.log"
rc=${PIPESTATUS[0]}

dmesg > "$OUTPUT_DIR/dmesg-after.txt"
grep -E "EXOFS|exofs|nvme|BUG:|WARNING:|Oops|Call Trace" "$OUTPUT_DIR/dmesg-after.txt" \
    > "$OUTPUT_DIR/dmesg-exofs.txt" 2>/dev/null
chmod -R a+rX "$OUTPUT_DIR"

echo
echo "==== exofs guest tests finished with rc=$rc, results in $OUTPUT_DIR ===="
grep -E "^(Ran|OK|FAILED)" "$OUTPUT_DIR/nosetests.log" || true
exit $rc
