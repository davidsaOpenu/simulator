#!/bin/bash
###############################################################################
# run_dod_tests.sh - Run all DoD guest tests for NVMe namespace support
# Runs inside the QEMU guest VM.
#
# Usage: cd /home/esd/guest && sudo ./run_dod_tests.sh
###############################################################################

set -o pipefail

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo ""
echo "============================================="
echo "  eVSSIM DoD Guest Tests                     "
echo "  NVMe Namespace Verification                "
echo "============================================="
echo ""

echo -e "${YELLOW}[PRE-FLIGHT] NVMe devices in /dev/:${NC}"
ls -la /dev/nvme* 2>/dev/null || echo "  No NVMe devices found!"
echo ""

echo -e "${YELLOW}[PRE-FLIGHT] nvme list:${NC}"
./nvme list 2>/dev/null || echo "  nvme list failed"
echo ""

echo -e "${YELLOW}[PRE-FLIGHT] Device map:${NC}"
python device_map.py 2>/dev/null || echo "  device_map.py not found or failed"
echo ""

OVERALL_EXIT=0

run_suite() {
    local suite_name="$1"
    local suite_dir="$2"

    echo ""
    echo -e "${YELLOW}=============================================${NC}"
    echo -e "${YELLOW}  Running: ${suite_name}${NC}"
    echo -e "${YELLOW}=============================================${NC}"

    if [ ! -d "$suite_dir" ]; then
        echo -e "${RED}  Directory $suite_dir not found, skipping.${NC}"
        return 1
    fi

    sudo VSSIM_NEXTGEN_BUILD_SYSTEM=1 \
        NVME_CLI_DIR="$SCRIPT_DIR" \
        PYTHONPATH="$SCRIPT_DIR:$PYTHONPATH" \
        nosetests -v "$suite_dir/" 2>&1

    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}  PASSED: ${suite_name}${NC}"
    else
        echo -e "${RED}  FAILED: ${suite_name} (exit code: ${exit_code})${NC}"
        OVERALL_EXIT=1
    fi

    return $exit_code
}

# Suite 1: Namespace Discovery
run_suite "Namespace Discovery" "namespace_tests"

# Suite 2: Sector Strategy (file I/O)
run_suite "Sector Strategy R/W" "sector_rw"

# Suite 3: Object Strategy (ioctl)
run_suite "Object Strategy R/W" "object_rw"

echo ""
echo "============================================="
if [ $OVERALL_EXIT -eq 0 ]; then
    echo -e "${GREEN}  ALL DOD TEST SUITES PASSED${NC}"
else
    echo -e "${RED}  SOME TEST SUITES FAILED${NC}"
fi
echo "============================================="
echo ""

exit $OVERALL_EXIT
