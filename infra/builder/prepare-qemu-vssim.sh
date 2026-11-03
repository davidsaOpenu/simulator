#!/bin/bash
set -euo pipefail

# QEMU 11's hw/vssim/meson.build compiles simulator sources through the
# gitignored hw/vssim/simulator view.  compile-qemu.sh runs git clean -fdx
# before every build, so regenerate regular-file copies from authoritative
# eVSSIM inputs after cleanup and before configuration. The caller supplies
# the source snapshot selected for the build; the generated copies are not
# authoritative sources and must not be edited or committed.

if [[ "$#" -ne 2 && ( "$#" -ne 3 || "${3:-}" != --legacy ) ]]; then
    echo "Usage: $0 <qemu-dir> <evssim-dir> [--legacy]"
    exit 1
fi

# readlink -f is also available in the legacy Ubuntu 14.04 image, whose
# coreutils package does not provide the realpath executable. This only
# resolves input paths; the generated source view uses cp, not symlinks.
QEMU_DIR="$(readlink -f -- "$1")"
EVSSIM_DIR="$(readlink -f -- "$2")"

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

if [ -L "$QEMU_DIR/hw" ] || [ -L "$VSSIM_DIR" ]; then
    echo "ERROR QEMU VSSIM parent directories must not be symlinks"
    exit 1
fi

# A failed copy/schema generation must not leave a partially prepared view.
# Replace the previous generated view only after all required inputs succeed.
STAGING_DIR="$(mktemp -d "$VSSIM_DIR/.simulator.XXXXXX")"
trap 'rm -rf -- "$STAGING_DIR"' EXIT

copy_evssim()
{
    local source_relative="$1"
    local target_name="$2"

    local source="$EVSSIM_DIR/$source_relative"
    local target="$STAGING_DIR/$target_name"

    if [ ! -f "$source" ] || [ -L "$source" ]; then
        echo "ERROR missing or non-regular eVSSIM source: $source"
        exit 1
    fi

    # Checking the file alone would still follow a linked source directory.
    local parent="$source_relative"
    while [[ "$parent" == */* ]]; do
        parent="${parent%/*}"
        if [ -L "$EVSSIM_DIR/$parent" ]; then
            echo "ERROR linked eVSSIM source directory: $EVSSIM_DIR/$parent"
            exit 1
        fi
    done

    mkdir -p -- "$(dirname "$target")"
    cp -- "$source" "$target"
}

# Keep these explicit source lists in sync with QEMU's hw/vssim/meson.build.
# Also include the headers/schema those sources need; Meson does not list
# every include. If an OSD include is added, update its copied dependencies
# here. Keep the www list below in sync with the monitor's required assets.
# Preserve relative include paths; omit the unrelated tgt tree and outputs.
for name in COPYING attr.c attr.h cdb.c cdb.h coll.c coll.h db.c db.h \
    list-entry.c list-entry.h mtq.c mtq.h obj.c obj.h osd-sense.c \
    osd-types.h osd.c osd.h target-sense.h osd.schema; do
    copy_evssim "osc-osd/osd-target/$name" "osc-osd/osd-target/$name"
done
for name in COPYING osd-util.c osd-util.h osd-defs.h osd-sense.h; do
    copy_evssim "osc-osd/osd-util/$name" "osc-osd/osd-util/$name"
done
OSD_SCHEMA_SOURCE="$STAGING_DIR/osc-osd/osd-target/osd.schema"
OSD_SCHEMA_TARGET="$STAGING_DIR/osd-schema.c"

{
    echo "const char osd_schema[] ="
    sed 's/^/"/; s/$/\\n"/' "$OSD_SCHEMA_SOURCE"
    echo ";"
} > "$OSD_SCHEMA_TARGET"

if [[ "${3:-}" == --legacy ]]; then
    # Legacy Makefile.objs compiles the schema from this historical location.
    cp -- "$OSD_SCHEMA_TARGET" "$STAGING_DIR/osc-osd/osd-target/osd-schema.c"
    # Older source revisions may still provide these files. Do not invent
    # replacements when they are absent: the legacy adapter checks its actual
    # Makefile inputs and reports any stale source-list entry explicitly.
    for name in ssd_file_operations.c ssd_file_operations.h; do
        relative="FTL_SOURCE/COMMON/ssd_file_operations/$name"
        if [[ -e "$EVSSIM_DIR/$relative" || -L "$EVSSIM_DIR/$relative" ]]; then
            copy_evssim "$relative" "$name"
        fi
    done
    for name in ftl_perf_manager.c ftl_perf_manager.h; do
        relative="FTL_SOURCE/PERF_MODULE/$name"
        if [[ -e "$EVSSIM_DIR/$relative" || -L "$EVSSIM_DIR/$relative" ]]; then
            copy_evssim "$relative" "$name"
        fi
    done
fi

# SSD module
copy_evssim "SSD_MODULE/ssd_io_manager.h" "ssd_io_manager.h"
copy_evssim "SSD_MODULE/ssd_io_manager.c" "ssd_io_manager.c"
copy_evssim "SSD_MODULE/test_context.h" "test_context.h"
copy_evssim "SSD_MODULE/test_context.c" "test_context.c"
copy_evssim "SSD_MODULE/ssd_log_manager.h" "ssd_log_manager.h"
copy_evssim "SSD_MODULE/ssd_log_manager.c" "ssd_log_manager.c"
copy_evssim "SSD_MODULE/ssd_util.h" "ssd_util.h"
copy_evssim "SSD_MODULE/onfi.h" "onfi.h"
copy_evssim "SSD_MODULE/onfi.c" "onfi.c"

# Common FTL code
copy_evssim "FTL_SOURCE/COMMON/common.h" "common.h"

# Page-map FTL
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl.h" "ftl.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl.c" "ftl.c"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.h" "ftl_sect_strategy.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_sect_strategy.c" "ftl_sect_strategy.c"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.h" "ftl_obj_strategy.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_obj_strategy.c" "ftl_obj_strategy.c"
copy_evssim "FTL_SOURCE/PAGE_MAP/TOOLS/uthash.h" "uthash.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_type.h" "ftl_type.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_gc_manager.h" "ftl_gc_manager.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_gc_manager.c" "ftl_gc_manager.c"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.h" \
    "ftl_inverse_mapping_manager.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_inverse_mapping_manager.c" \
    "ftl_inverse_mapping_manager.c"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.h" "ftl_mapping_manager.h"
copy_evssim "FTL_SOURCE/PAGE_MAP/ftl_mapping_manager.c" "ftl_mapping_manager.c"

# VSSIM configuration
copy_evssim "CONFIG/vssim_config_manager.h" "vssim_config_manager.h"
copy_evssim "CONFIG/vssim_config_manager.c" "vssim_config_manager.c"

# Monitor
for name in index.html index.css favicon.ico; do
    copy_evssim "MONITOR/SERVER/www/$name" "www/$name"
done

# Logging
copy_evssim "LOG_MGR/logging_backend.h" "logging_backend.h"
copy_evssim "LOG_MGR/logging_backend.c" "logging_backend.c"
copy_evssim "LOG_MGR/logging_manager.h" "logging_manager.h"
copy_evssim "LOG_MGR/logging_manager.c" "logging_manager.c"
copy_evssim "LOG_MGR/logging_parser.h" "logging_parser.h"
copy_evssim "LOG_MGR/logging_parser.c" "logging_parser.c"
copy_evssim "LOG_MGR/logging_statistics.h" "logging_statistics.h"
copy_evssim "LOG_MGR/logging_statistics.c" "logging_statistics.c"
copy_evssim "LOG_MGR/logging_rt_analyzer.h" "logging_rt_analyzer.h"
copy_evssim "LOG_MGR/logging_rt_analyzer.c" "logging_rt_analyzer.c"
copy_evssim "MONITOR/SERVER/logging_server.h" "logging_server.h"
copy_evssim "MONITOR/SERVER/logging_server.c" "logging_server.c"
copy_evssim "LOG_MGR/logging_offline_analyzer.h" "logging_offline_analyzer.h"
copy_evssim "LOG_MGR/logging_offline_analyzer.c" "logging_offline_analyzer.c"

rm -rf -- "$VSSIM_TARGET"
mv -- "$STAGING_DIR" "$VSSIM_TARGET"
trap - EXIT

echo "INFO prepared VSSIM sources in $VSSIM_TARGET"
