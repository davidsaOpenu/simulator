#!/bin/bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <qemu-dir> <evssim-dir>"
    exit 1
fi

QEMU_DIR="$(realpath "$1")"
EVSSIM_DIR="$(realpath "$2")"

VSSIM_DIR="$QEMU_DIR/hw/vssim"
VSSIM_TARGET="$VSSIM_DIR/simulator"

if [ ! -d "$VSSIM_DIR" ]; then
    echo "ERROR VSSIM QEMU directory does not exist: $VSSIM_DIR"
    exit 1
fi

if [ ! -d "$EVSSIM_DIR" ]; then
    echo "ERROR eVSSIM source directory does not exist: $EVSSIM_DIR"
    exit 1
fi

rm -rf "$VSSIM_TARGET"
mkdir -p "$VSSIM_TARGET"

link_evssim()
{
    local source_relative="$1"
    local target_name="$2"

    local source="$EVSSIM_DIR/$source_relative"
    local target="$VSSIM_TARGET/$target_name"

    if [ ! -e "$source" ]; then
        echo "ERROR missing eVSSIM source: $source"
        exit 1
    fi

    # Use a relative link so it works both on the host and when the
    # project root is mounted at a different path inside Docker.
    local relative_source
    relative_source="$(realpath --relative-to="$(dirname "$target")" "$source")"

    ln -s "$relative_source" "$target"
}

# OSD source tree
link_evssim "osc-osd" "osc-osd"
OSD_SCHEMA_SOURCE="$EVSSIM_DIR/osc-osd/osd-target/osd.schema"
OSD_SCHEMA_TARGET="$VSSIM_TARGET/osd-schema.c"

{
    echo "const char osd_schema[] ="
    sed 's/^/"/; s/$/\\n"/' "$OSD_SCHEMA_SOURCE"
    echo ";"
} > "$OSD_SCHEMA_TARGET"

# SSD module
link_evssim "SSD_MODULE/ssd_io_manager.h" "ssd_io_manager.h"
link_evssim "SSD_MODULE/ssd_io_manager.c" "ssd_io_manager.c"
link_evssim "SSD_MODULE/test_context.h" "test_context.h"
link_evssim "SSD_MODULE/test_context.c" "test_context.c"
link_evssim "SSD_MODULE/ssd_log_manager.h" "ssd_log_manager.h"
link_evssim "SSD_MODULE/ssd_log_manager.c" "ssd_log_manager.c"
link_evssim "SSD_MODULE/ssd_util.h" "ssd_util.h"
link_evssim "SSD_MODULE/onfi.h" "onfi.h"
link_evssim "SSD_MODULE/onfi.c" "onfi.c"

# Common FTL code
link_evssim "FTL_SOURCE/COMMON/common.h" "common.h"
link_evssim "FTL_SOURCE/COMMON/ssd_file_operations/ssd_file_operations.h" \
    "ssd_file_operations.h"
link_evssim "FTL_SOURCE/COMMON/ssd_file_operations/ssd_file_operations.c" \
    "ssd_file_operations.c"

# Page-map FTL
link_evssim "FTL_SOURCE/PAGE_MAP/ftl.h" "ftl.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl.c" "ftl.c"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.h" "ftl_sect_strategy.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.c" "ftl_sect_strategy.c"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.h" "ftl_obj_strategy.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.c" "ftl_obj_strategy.c"
link_evssim "FTL_SOURCE/PAGE_MAP/TOOLS/uthash.h" "uthash.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_type.h" "ftl_type.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_gc_manager.h" "ftl_gc_manager.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_gc_manager.c" "ftl_gc_manager.c"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.h" \
    "ftl_inverse_mapping_manager.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.c" \
    "ftl_inverse_mapping_manager.c"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.h" "ftl_mapping_manager.h"
link_evssim "FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.c" "ftl_mapping_manager.c"

# FTL performance module
link_evssim "FTL_SOURCE/PERF_MODULE/ftl_perf_manager.h" "ftl_perf_manager.h"
link_evssim "FTL_SOURCE/PERF_MODULE/ftl_perf_manager.c" "ftl_perf_manager.c"

# VSSIM configuration
link_evssim "CONFIG/vssim_config_manager.h" "vssim_config_manager.h"
link_evssim "CONFIG/vssim_config_manager.c" "vssim_config_manager.c"

# Monitor
link_evssim "MONITOR/SERVER/www" "www"

# Logging
link_evssim "LOG_MGR/logging_backend.h" "logging_backend.h"
link_evssim "LOG_MGR/logging_backend.c" "logging_backend.c"
link_evssim "LOG_MGR/logging_manager.h" "logging_manager.h"
link_evssim "LOG_MGR/logging_manager.c" "logging_manager.c"
link_evssim "LOG_MGR/logging_parser.h" "logging_parser.h"
link_evssim "LOG_MGR/logging_parser.c" "logging_parser.c"
link_evssim "LOG_MGR/logging_statistics.h" "logging_statistics.h"
link_evssim "LOG_MGR/logging_statistics.c" "logging_statistics.c"
link_evssim "LOG_MGR/logging_rt_analyzer.h" "logging_rt_analyzer.h"
link_evssim "LOG_MGR/logging_rt_analyzer.c" "logging_rt_analyzer.c"
link_evssim "MONITOR/SERVER/logging_server.h" "logging_server.h"
link_evssim "MONITOR/SERVER/logging_server.c" "logging_server.c"
link_evssim "LOG_MGR/logging_offline_analyzer.h" "logging_offline_analyzer.h"
link_evssim "LOG_MGR/logging_offline_analyzer.c" "logging_offline_analyzer.c"

echo "INFO prepared VSSIM sources in $VSSIM_TARGET"
