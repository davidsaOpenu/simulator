#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

# Host-test logs go to a throwaway folder instead of the workspace logs folder.
# Now that the logger no longer drops events, the host tests write several times
# more logs than before. CI ships the whole logs folder into the podman VM, where
# the extra volume fills the disk until Elasticsearch cannot allocate its shards
# and the stack never comes up. Nothing in this stage reads the host-test logs,
# and the guest and EXOFS logs are still shipped.
LOGS_TMP="$(mktemp -d)"
trap 'rm -rf "$LOGS_TMP"' EXIT
export EVSSIM_DOCKER_XOPTIONS="${EVSSIM_DOCKER_XOPTIONS:-} -v $LOGS_TMP:$EVSSIM_DOCKER_ROOT_PATH/logs"

evssim_run_at_folder "$version" $EVSSIM_SIMULATOR_FOLDER/eVSSIM/tests/host ./run_all_host_tests.sh
