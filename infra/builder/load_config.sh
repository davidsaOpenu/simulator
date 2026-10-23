#!/bin/bash
# ============================================================
# Load configuration from JSON
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Helper function to load and validate a JSON value
load_json_value() {
    local var_name=$1
    local jq_query=$2
    local config_json=$3

    local value
    if ! value=$(jq -er "$jq_query | select(type == \"string\" and length > 0)" <<< "$config_json"); then
        echo "ERROR: Failed to load $var_name" >&2
        return 1
    fi

    printf -v "$var_name" '%s' "$value"
    export "$var_name"
    return 0
}

# Load multiple fields from a config section using a loop
load_config_section() {
    local section_name=$1
    local jq_base_path=$2
    local config_json=$3
    shift 3
    local fields=("$@")

    for field in "${fields[@]}"; do
        # Convert camelCase to UPPER_SNAKE_CASE
        # compileContainer -> COMPILE_CONTAINER, guestVMImage -> GUEST_VM_IMAGE
        local field_upper=$(echo "$field" | sed 's/\([A-Z]\)/_\1/g' | sed 's/^_//' | tr '[:lower:]' '[:upper:]')
        local var_name="EVSSIM_${section_name}_${field_upper}"
        local jq_path="${jq_base_path}.${field}"
        load_json_value "$var_name" "$jq_path" "$config_json" || return 1
    done

    return 0
}

# Load config from JSON using jq
load_config() {
    local config_id=""
    local config_json
    local directory="${SCRIPT_DIR}/${EVSSIM_VERSIONS_CONFIGURATION:-versions}"
    if [[ $# -gt 0 ]]; then
        config_id=$1
        [[ "$config_id" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: Invalid configuration ID" >&2; return 1; }
    elif [[ -n "${EVSSIM_VERSIONS_CONFIGURATION_ID+x}" ]]; then
        config_id=$EVSSIM_VERSIONS_CONFIGURATION_ID
        [[ "$config_id" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: Invalid configuration ID" >&2; return 1; }
    fi

    if [[ -n "${EVSSIM_QEMU_INPUT_RECORD:-}" ]]; then
        # Use the full frozen profile, not a later version of the same ID.
        config_json=$(python3 "$SCRIPT_DIR/qemu-artifact.py" \
            --profile-from-inputs "$EVSSIM_QEMU_INPUT_RECORD") || return 1
        local recorded_id
        recorded_id=$(jq -er '.id' <<< "$config_json") || return 1
        if [[ -n "$config_id" && "$config_id" != "$recorded_id" ]]; then
            echo "ERROR: Configuration ID conflicts with the resolved input record" >&2
            return 1
        fi
        config_id=$recorded_id
    else
        if [[ -z "$config_id" ]]; then
            config_id=$(jq -er '.default_profile' "$directory/qemu-build-policy.json") || return 1
        fi
        [[ "$config_id" =~ ^[1-9][0-9]*$ ]] || { echo "ERROR: Invalid default configuration ID" >&2; return 1; }
        if ! config_json=$(jq -ce --argjson id "$config_id" \
            '[.configs[] | select(.id == $id)] | if length == 1 then .[0] else error("configuration ID missing or duplicated") end' \
            "$directory/configs.json"); then
            echo "ERROR: Failed to load configuration $config_id" >&2
            return 1
        fi
    fi

    # Load component configurations using the loop function
    load_config_section "KERNEL" '.kernel' "$config_json" "branch" "compileContainer" || return 1
    load_config_section "QEMU" '.qemu' "$config_json" "branch" "compileContainer" || return 1
    load_config_section "NVME_CLI" '.["nvme-cli"]' "$config_json" "branch" "compileContainer" || return 1
    load_config_section "HOST_TESTS" '.hostTests' "$config_json" "compileContainer" "runContainer" || return 1
    load_config_section "GUEST_TESTS" '.guestTests' "$config_json" "guestVmImage" "guestVmBuildContainer" "compileContainer" || return 1
    export EVSSIM_VERSIONS_CONFIGURATION_ID="$config_id"

    # Print configuration summary
    echo "=========================================="
    echo "Configuration loaded (ID: $config_id)"
    echo "=========================================="
    echo ""
    echo "Kernel:"
    echo "  Branch: $EVSSIM_KERNEL_BRANCH"
    echo "  Compile Container: $EVSSIM_KERNEL_COMPILE_CONTAINER"
    echo ""
    echo "QEMU:"
    echo "  Branch: $EVSSIM_QEMU_BRANCH"
    echo "  Compile Container: $EVSSIM_QEMU_COMPILE_CONTAINER"
    echo ""
    echo "nvme-cli:"
    echo "  Branch: $EVSSIM_NVME_CLI_BRANCH"
    echo "  Compile Container: $EVSSIM_NVME_CLI_COMPILE_CONTAINER"
    echo ""
    echo "Host Tests:"
    echo "  Compile Container: $EVSSIM_HOST_TESTS_COMPILE_CONTAINER"
    echo "  Run Container: $EVSSIM_HOST_TESTS_RUN_CONTAINER"
    echo ""
    echo "Guest Tests:"
    echo "  Compile Container: $EVSSIM_GUEST_TESTS_COMPILE_CONTAINER"
    echo "  VM Image: $EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"
    echo "  VM Image Build Container: $EVSSIM_GUEST_TESTS_GUEST_VM_BUILD_CONTAINER"
    echo "=========================================="

    return 0
}

if ! load_config; then
    echo "ERROR: Failed to load configuration"
    return 1 2>/dev/null || exit 1
fi
