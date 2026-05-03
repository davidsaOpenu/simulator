#!/bin/bash
# ============================================================
# Load configuration from JSON
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Helper function to load and validate a JSON value
load_json_value() {
    local var_name=$1
    local jq_query=$2
    local config_file=$3

    local value=$(jq -r "$jq_query" "$config_file" 2>/dev/null)

    if [[ -z "$value" || "$value" == "null" ]]; then
        echo "ERROR: Failed to load $var_name"
        return 1
    fi

    eval "export $var_name='$value'"
    return 0
}

# Load multiple fields from a config section using a loop
load_config_section() {
    local section_name=$1
    local jq_base_path=$2
    local config_file=$3
    shift 3
    local fields=("$@")

    for field in "${fields[@]}"; do
        # Convert camelCase to UPPER_SNAKE_CASE
        # compileContainer -> COMPILE_CONTAINER, guestVMImage -> GUEST_VM_IMAGE
        local field_upper=$(echo "$field" | sed 's/\([A-Z]\)/_\1/g' | sed 's/^_//' | tr '[:lower:]' '[:upper:]')
        local var_name="EVSSIM_${section_name}_${field_upper}"
        local jq_path="${jq_base_path}.${field}"
        load_json_value "$var_name" "$jq_path" "$config_file" || return 1
    done

    return 0
}

# Resolve image references in a string
resolve_image_references() {
    local text=$1
    local config_file=$2

    # Iterate through all images in config and replace references
    while IFS='=' read -r key value; do
        text="${text//\$images.$key/$value}"
    done < <(jq -r '.images | to_entries[] | "\(.key)=\(.value)"' "$config_file" 2>/dev/null)

    echo "$text"
}

# Load config from JSON using jq
load_config() {
    local config_id=$1
    local config_file="${SCRIPT_DIR}/$EVSSIM_VERSIONS_CONFIGURATION/configs.json"

    if [[ ! -f "$config_file" ]]; then
        echo "ERROR: Config file not found: $config_file"
        return 1
    fi

    # Check if config ID exists
    if ! jq -e ".configs[] | select(.id == $config_id)" "$config_file" > /dev/null 2>&1; then
        echo "ERROR: Config ID $config_id not found in $config_file"
        return 1
    fi

    # Define the base jq query for the selected config
    local cfg_query=".configs[] | select(.id == $config_id)"

    # Load all image URLs
    while IFS='=' read -r key value; do
        local var_name="EVSSIM_IMAGE_${key//./_}"
        #eval "export $var_name='$value'"
    done < <(jq -r '.images | to_entries[] | "\(.key)=\(.value)"' "$config_file" 2>/dev/null)

    # Load component configurations using the loop function
    load_config_section "KERNEL" "$cfg_query | .kernel" "$config_file" "branch" "compileContainer" || return 1
    load_config_section "QEMU" "$cfg_query | .qemu" "$config_file" "branch" "compileContainer" || return 1
    load_config_section "NVME_CLI" "$cfg_query | .[\"nvme-cli\"]" "$config_file" "branch" "compileContainer" || return 1
    load_config_section "HOST_TESTS" "$cfg_query | .hostTests" "$config_file" "compileContainer" "runContainer" || return 1
    load_config_section "GUEST_TESTS" "$cfg_query | .guestTests" "$config_file" "compileContainer" || return 1

    # Load and resolve guest VM image reference dynamically
    local guest_vm_ref=$(jq -r "$cfg_query | .guestTests.guestVMImage" "$config_file" 2>/dev/null)
    if [[ -z "$guest_vm_ref" || "$guest_vm_ref" == "null" ]]; then
        echo "ERROR: Failed to load guest VM image reference"
        return 1
    fi
    export EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE=$(resolve_image_references "$guest_vm_ref" "$config_file")

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
    echo "=========================================="

    return 0
}

# Load default config (config 2)
if ! load_config 2; then
    echo "ERROR: Failed to load configuration"
    exit 1
fi
