#!/bin/bash
set -Eeo pipefail
trap 'ec=$?; echo "[run-ci.sh] FAILED with exit $ec on: $BASH_COMMAND" >&2' ERR

# Resolve caller-supplied paths before changing to the builder directory.
if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
    EVSSIM_QEMU_INPUT_RECORD=$(realpath "$EVSSIM_QEMU_INPUT_RECORD")
fi
if [[ -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]]; then
    EVSSIM_DEPENDENCY_RECORD=$(realpath "$EVSSIM_DEPENDENCY_RECORD")
fi
requested_profile=${EVSSIM_VERSIONS_CONFIGURATION_ID:-}

# always run from the builder dir so env.sh works
cd "$(dirname "${BASH_SOURCE[0]}")"

source ./env.sh

# The existing Jenkins caller does not pass a report path between its shells.
# Discover only this build/change/revision's report. Explicit inputs win, and
# local runs do not consume reports left by CI.
if [[ -z "${EVSSIM_DEPENDENCY_RECORD:-}" && -z "${EVSSIM_QEMU_INPUT_RECORD:-}" &&
      -n "${BUILD_TAG:-}" && -n "${GERRIT_CHANGE_NUMBER:-}" &&
      -n "${GERRIT_PATCHSET_REVISION:-}" ]]; then
    report_key=$(printf '%s\n' "$BUILD_TAG" "$GERRIT_CHANGE_NUMBER" "$GERRIT_PATCHSET_REVISION" | sha256sum)
    EVSSIM_DEPENDENCY_RECORD="$EVSSIM_ROOT_PATH/logs/qemu11-validation/dependencies-${report_key%% *}.json"
fi

# Keep each invocation's frozen inputs and selected artifact together.
mkdir -p "$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER/qemu11-validation"
workflow_dir=$(mktemp -d "$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER/qemu11-validation/run.XXXXXXXX")
echo "INFO Workflow inputs and artifact selection: $workflow_dir"
workflow_helper="$PWD/qemu-workflow.py"
ELK_DIR="$EVSSIM_ROOT_PATH/simulator/infra/ELK"
ELK_INSTALL="$ELK_DIR/install_and_start_elk.sh"
ELK_CLEAN="$ELK_DIR/elk_cleanup.sh"

run_required() {
    python3 "$workflow_helper" --directory "$workflow_dir" stage "$@"
}

finish_workflow() {
    local status=$?
    trap - EXIT
    # ELK cleanup remains best effort; its result is recorded separately and
    # cannot replace the required command's exit status.
    run_required cleanup -- "$ELK_CLEAN" --complete-cleanup || true
    local result=$status finish_status=0 export_status=0
    python3 "$workflow_helper" --directory "$workflow_dir" finish "$status" || finish_status=$?
    if [[ "$result" -eq 0 ]]; then result=$finish_status; fi
    # Jenkins replaces the workspace on its next checkout. Retain this run's
    # curated evidence in a unique sibling directory without changing the job.
    local durable="$EVSSIM_ROOT_PATH@qemu11-validation/${workflow_dir##*/}"
    local export_options=()
    if [[ "$result" -ne 0 ]]; then export_options=(--allow-incomplete); fi
    echo "INFO Evidence destination: $durable"
    python3 "$workflow_helper" --directory "$workflow_dir/export" export \
        --workspace "$EVSSIM_ROOT_PATH" --run "$workflow_dir" --durable "$durable" \
        "${export_options[@]}" || export_status=$?
    # Evidence failure fails a successful run, but never replaces a test error.
    if [[ "$result" -eq 0 ]]; then result=$export_status; fi
    exit "$result"
}
trap finish_workflow EXIT
trap 'exit 143' TERM
trap 'exit 130' INT

if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" && -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]]; then
    echo "ERROR: Supply either saved QEMU inputs or a dependency report, not both." | tee "$workflow_dir/input-error.log" >&2
    exit 1
fi

if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
    cp "$EVSSIM_QEMU_INPUT_RECORD" "$workflow_dir/inputs.json"
    run_required --stdout "$workflow_dir/profile.json" resolve-inputs -- \
        python3 ./qemu-artifact.py --profile-from-inputs "$workflow_dir/inputs.json"
    selected_profile=$(jq -er '.id' "$workflow_dir/profile.json")
    if [[ -n "$requested_profile" && "$requested_profile" != "$selected_profile" ]]; then
        echo "ERROR: Requested profile conflicts with the saved QEMU inputs." | tee "$workflow_dir/input-error.log" >&2
        exit 1
    fi
    artifact_inputs=(--saved-inputs "$workflow_dir/inputs.json" --check-environment)
else
    profile_arguments=()
    if [[ -n "${EVSSIM_DEPENDENCY_RECORD:-}" ]]; then
        cp "$EVSSIM_DEPENDENCY_RECORD" "$workflow_dir/dependencies.json"
        if [[ -n "$requested_profile" ]]; then
            profile_arguments=(--profile-id "$requested_profile")
        fi
    else
        # A Gerrit caller must supply its dependency outcomes. A local run
        # without dependency metadata retains the loaded shell default (#5).
        if [[ -n "${GERRIT_CHANGE_NUMBER:-}" ]]; then
            echo "ERROR: Gerrit workflow requires a dependency report or saved QEMU inputs." | tee "$workflow_dir/input-error.log" >&2
            exit 1
        fi
        printf '%s\n' '{"schema_version":1,"status":"complete","dependencies":[]}' > "$workflow_dir/dependencies.json"
        profile_arguments=(--profile-id "$EVSSIM_VERSIONS_CONFIGURATION_ID")
    fi
    run_required --stdout "$workflow_dir/resolved-inputs.json" resolve-inputs -- \
        ./compile-qemu.sh --resolve-dependencies "$workflow_dir/dependencies.json" \
        --qemu-dir "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" \
        --simulator-dir "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER" \
        "${profile_arguments[@]}"
    selected_profile=$(jq -er '.profile.id' "$workflow_dir/resolved-inputs.json")
    artifact_inputs=(--resolved-inputs "$workflow_dir/resolved-inputs.json")
fi

# The resolver chooses an existing configuration, never per-component overrides.
export EVSSIM_VERSIONS_CONFIGURATION_ID="$selected_profile"
source ./load_config.sh
run_required workflow-inputs -- python3 "$workflow_helper" --directory "$workflow_dir" inputs

# Check all required tools are installed after resolving the effective profile.
run_required check-tools -- bash ./check_tools.sh
export EVSSIM_RUNTIME_ALWAYS_RESET=yes

# freeze absolute paths so later env changes can't break us
LOGS_DIR="$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER"

# sanity checks
[[ -x "$ELK_INSTALL" ]] || { echo "Missing: $ELK_INSTALL"; exit 1; }
[[ -x "$ELK_CLEAN"   ]] || { echo "Missing: $ELK_CLEAN";   exit 1; }

mkdir -p "$LOGS_DIR"

# Run tox
run_required tox -- env -u http_proxy -u https_proxy -u HTTP_PROXY -u HTTPS_PROXY tox

# build + sanity
run_required build-images -- ./build-docker-image.sh
run_required container-images -- python3 "$workflow_helper" --directory "$workflow_dir" images
run_required qemu-sources -- ./prepare-qemu-sources.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"
# Use the selected profile directly: its declared guest settings describe
# the actual build inputs, without profile-specific overrides in this script.
run_required guest-image -- ./build-qemu-image.sh "$EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE" "$EVSSIM_GUEST_TESTS_GUEST_VM_BUILD_CONTAINER"
run_required kernel -- ./compile-kernel.sh "$EVSSIM_KERNEL_COMPILE_CONTAINER"
# Build or verify/reuse one bundle. Every subsequent QEMU launch consumes this
# selection; a cache hit does not bypass the other builds or any required test.
# Keep generated build links outside logs, which the Podman stage copies whole.
run_required --stdout "$workflow_dir/artifact-selection.json" qemu-artifact -- \
    python3 ./qemu-artifact.py "${artifact_inputs[@]}" \
    --qemu-dir "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" \
    --simulator-dir "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER" \
    --build-dir "$EVSSIM_ROOT_PATH/artifacts/qemu-builds/${workflow_dir##*/}" --jobs "$(nproc)" \
    --artifact-root "$EVSSIM_ROOT_PATH/artifacts" \
    --dist-root "$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER"
export EVSSIM_QEMU_ARTIFACT_SELECTION="$workflow_dir/artifact-selection.json"
run_required host-tools -- ./compile-host-tests.sh "$EVSSIM_HOST_TESTS_COMPILE_CONTAINER"
run_required guest-tools -- ./compile-guest-tests.sh "$EVSSIM_GUEST_TESTS_COMPILE_CONTAINER"
# These tools used to be a side effect of compiling legacy QEMU. They remain
# required when QEMU comes from the modern recipe or an existing cached bundle.
run_required exofs-tools -- ./compile-exofs-tools.sh "$EVSSIM_GUEST_TESTS_COMPILE_CONTAINER"
run_required sanity -- ./docker-run-sanity.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"

# start ELK (absolute paths)
run_required elk-start -- "$ELK_INSTALL" "$LOGS_DIR" "$ELK_DIR"

# Running Docker Tests
run_required host-tests -- ./docker-test-host.sh "$EVSSIM_HOST_TESTS_RUN_CONTAINER"
run_required host-metrics -- ./docker-test-host-elk.sh "$EVSSIM_HOST_TESTS_RUN_CONTAINER"
run_required guest-tests -- ./docker-test-guest.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"
run_required exofs -- ./docker-test-exofs.sh "$EVSSIM_QEMU_COMPILE_CONTAINER"
