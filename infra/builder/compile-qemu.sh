#!/bin/bash
set -e

# S2b resolves source/profile inputs without builder.sh side effects or a build.
if [ "${1:-}" = "--resolve-dependencies" ]; then
    exec python3 "$(dirname "$(realpath "$0")")/qemu-inputs.py" "$@"
fi
# Artifact calls consume explicit inputs before builder.sh can mutate sources.
# Keep the selection-only entry point above unchanged; S2c adds its consumer.
if [ "${1:-}" = "--request" ] || [ "${1:-}" = "--resolved-inputs" ]; then
    exec python3 "$(dirname "$(realpath "$0")")/qemu-artifact.py" "$@"
fi

# A container is a build environment, not a source-branch selector. Reject
# conflicting callers before builder.sh or any checkout/cleanup side effects.
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <ubuntu-14.04|ubuntu-26.04>" >&2
    exit 1
fi
version="${1:-}"
if [[ -z "${EVSSIM_QEMU_BRANCH:-}" || -z "${EVSSIM_QEMU_COMPILE_CONTAINER:-}" ]]; then
    echo "ERROR: Load the selected configuration with source ./env.sh first" >&2
    exit 1
fi
if [[ "$version" != "$EVSSIM_QEMU_COMPILE_CONTAINER" ]]; then
    echo "ERROR: Requested container $version conflicts with selected profile $EVSSIM_VERSIONS_CONFIGURATION_ID ($EVSSIM_QEMU_COMPILE_CONTAINER)" >&2
    exit 1
fi
if [[ "$version" != ubuntu-14.04 && "$version" != ubuntu-26.04 ]]; then
    echo "ERROR: Unsupported QEMU compile container: $version" >&2
    exit 1
fi
git check-ref-format --branch "$EVSSIM_QEMU_BRANCH" >/dev/null
if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
    if [[ "$version" == ubuntu-14.04 ]]; then
        echo "ERROR: Saved-input legacy artifact selection is QEMU-only. Use qemu-artifact.py explicitly; this version wrapper must also build exofs tools. Independent tool integration belongs to S4/S7." >&2
        exit 1
    fi
    # Use the saved record's immutable image and recipe in a disposable source
    # workspace. A cache hit must never bypass the legacy tool-building tail.
    saved_job_id=$(python3 -c 'import uuid; print(uuid.uuid4().hex)')
    exec python3 "$(dirname "$(realpath "$0")")/qemu-artifact.py" \
        --saved-inputs "$EVSSIM_QEMU_INPUT_RECORD" --check-environment \
        --qemu-dir "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" \
        --simulator-dir "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER" \
        --build-dir "$EVSSIM_ROOT_PATH/$EVSSIM_LOGS_FOLDER/qemu-$saved_job_id" \
        --jobs "$(nproc)" \
        --artifact-root "$EVSSIM_ROOT_PATH/artifacts" \
        --dist-root "$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER"
fi

# Read the same recipe used by artifact builds before ordinary build setup.
# Shell quoting preserves each configure argument through builder.sh's bash -c.
qemu_artifact_helper="$(dirname "$(realpath "$0")")/qemu-artifact.py"
qemu_configure_command=$(python3 "$qemu_artifact_helper" --configure-command "$version")

source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"

evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make MK_PATH=. ARCH=x86_64 clean"

# Setup git branch, and cleanup artifacts on host
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" fetch --all --prune
echo "INFO building QEMU branch $EVSSIM_QEMU_BRANCH, profile $EVSSIM_VERSIONS_CONFIGURATION_ID, container $version"
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" checkout "$EVSSIM_QEMU_BRANCH"

# Necessary since builds interfere with each other due to artifacts
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" pull --ff-only
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" reset --hard
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" clean -fdx
git -C "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" submodule update --init --recursive

if [ "$version" = "ubuntu-26.04" ]; then
    # Prepare exact wrap revisions before the shared --disable-download flag.
    # Artifact builds use the same helper with their local source-object cache.
    evssim_run_at_folder "$version" "$EVSSIM_QEMU_FOLDER" \
        python3 "$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/builder/qemu-artifact.py" \
        --prepare-subprojects .
    "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/infra/builder/prepare-qemu-vssim.sh" \
        "$EVSSIM_ROOT_PATH/$EVSSIM_QEMU_FOLDER" \
        "$EVSSIM_ROOT_PATH/$EVSSIM_SIMULATOR_FOLDER/eVSSIM"
fi

# Configure with the policy's explicit target and ordered argument list.
evssim_run_at_folder "$version" "$EVSSIM_QEMU_FOLDER" "$qemu_configure_command"

# Make
if [ "$version" = "ubuntu-26.04" ]; then
    # Note: build with ninja for 26.04
    evssim_run_at_folder "$version" "$EVSSIM_QEMU_FOLDER" bear -- ninja -C build -j$(nproc)
    echo "INFO 26.04 QEMU build complete. Skipping legacy open-osd/exofs build."
    exit 0
fi

evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER make clean
evssim_run_at_folder "$version" $EVSSIM_QEMU_FOLDER bear -- make -j$(nproc)

# Build osc-osd
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/osc-osd" "make MK_PATH=. ARCH=x86_64 clean && \
    make MK_PATH=. ARCH=x86_64 -j\`nproc\` && \
    rsync -av --progress --exclude='.git' ./ $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/osc-osd/"

# Build mkfs.exofs executable and the shared lib libosd (required by mkfs.exofs)
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/" "git submodule update --init --recursive && \
    git submodule foreach --recursive 'git reset --hard && git clean -fdx'"

evssim_run_at_folder "$version" "open-osd" "make KSRC=/code/kernel ARCH=x86_64 clean && \
    make KSRC=$EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_KERNEL_FOLDER -j\`nproc\` && \
    cp usr/mkfs.exofs $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/mkfs.exofs && \
    cp lib/libosd.so $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/libosd.so"

# Copy OSD emulation and exofs setup script into the dist directory
evssim_run_at_folder "$version" "$EVSSIM_SIMULATOR_FOLDER/eVSSIM/scripts" "cp -r exofs \
    $EVSSIM_DOCKER_ROOT_PATH/$EVSSIM_DIST_FOLDER/exofs"
