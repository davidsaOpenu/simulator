#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

# the only script we source on purpose
source ./env.sh
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

# freeze absolute paths so later env changes can't break us
LOGS_DIR="$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER"
ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
ELK_INSTALL="$ELK_DIR/install_and_start_elk.sh"
ELK_CLEAN="$ELK_DIR/elk_cleanup.sh"

# sanity checks
[[ -x "$ELK_INSTALL" ]] || { echo "Missing: $ELK_INSTALL"; exit 1; }
[[ -x "$ELK_CLEAN"   ]] || { echo "Missing: $ELK_CLEAN";   exit 1; }

mkdir -p "$LOGS_DIR"

# static trap string (evaluated NOW, not later)
trap "$ELK_CLEAN --complete-cleanup || true" EXIT

# Run tox
env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY tox

# # build + sanity
./build-docker-image.sh
./build-qemu-image.sh
./compile-kernel.sh
./compile-qemu.sh
./compile-host-tests.sh
./compile-guest-tests.sh
./docker-run-sanity.sh

# # start ELK (absolute paths)
"$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# # tests
./docker-test-host.sh
# ./docker-test-guest.sh
# ./docker-test-exofs.sh

# # Testing ELK performance
# "$ELK_DIR/elk_performance_test.sh" "$ELK_DIR"

# Print environment variables for debugging
echo "================================================================================"
echo "[run-ci.sh] ENVIRONMENT VARIABLES DEBUG INFO"
echo "================================================================================"
echo "[run-ci.sh] Key paths:"
echo "  EVSSIM_ROOT_PATH=$EVSSIM_ROOT_PATH"
echo "  EVSSIM_LOGS_FOLDER=$EVSSIM_LOGS_FOLDER"
echo "  LOGS_DIR=$LOGS_DIR"
echo "  ELK_DIR=$ELK_DIR"
echo "  ELK_INSTALL=$ELK_INSTALL"
echo "  ELK_CLEAN=$ELK_CLEAN"
echo ""
echo "[run-ci.sh] All EVSSIM_* environment variables:"
env | grep '^EVSSIM_' | sort | sed 's/^/  /'
echo ""
echo "[run-ci.sh] All ELK_* environment variables:"
env | grep '^ELK_' | sort | sed 's/^/  /'
echo ""
echo "[run-ci.sh] Filebeat/ELK related variables:"
env | grep -iE '(filebeat|elastic|kibana|logstash)' | sort | sed 's/^/  /'
echo ""
echo "[run-ci.sh] Checking log files in LOGS_DIR:"
if [[ -d "$LOGS_DIR" ]]; then
  echo "  LOGS_DIR exists: $LOGS_DIR"
  echo "  Log files found:"
  find "$LOGS_DIR" -name "elk_log_file-*.log" -type f 2>/dev/null | head -10 | sed 's/^/    /' || echo "    (none found)"
  echo "  All files in LOGS_DIR:"
  ls -lah "$LOGS_DIR" 2>/dev/null | head -20 | sed 's/^/    /' || echo "    (directory empty or not accessible)"
else
  echo "  LOGS_DIR does not exist: $LOGS_DIR"
fi
echo ""
echo "[run-ci.sh] Current working directory: $(pwd)"
echo "[run-ci.sh] User: $(whoami)"
echo "[run-ci.sh] Hostname: $(hostname)"
echo "================================================================================"
