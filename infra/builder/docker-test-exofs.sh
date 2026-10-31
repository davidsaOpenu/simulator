#!/bin/bash
source ./builder.sh

evssim_validate_version_arguments "$0" "${1:-}" "$#"
version="$1"

verify_exofs_tools() {
    # S4's independent build publishes this record only after staging succeeds.
    # Check it before image replacement so mixed/stale tools cannot reach a guest.
    python3 - "$EVSSIM_ROOT_PATH" "$EVSSIM_DIST_FOLDER" "$EVSSIM_VERSIONS_FOLDER" \
        "$EVSSIM_VERSIONS_CONFIGURATION_ID" "$EVSSIM_DOCKER_IMAGE_NAME" \
        "$EVSSIM_SIMULATOR_FOLDER" "$EVSSIM_KERNEL_FOLDER" <<'PY'
import hashlib
import json
from pathlib import Path
import subprocess
import sys

try:
    root = Path(sys.argv[1])
    dist = root / sys.argv[2]
    record = json.loads((dist / "exofs-tools.json").read_text())
    profiles = json.loads((root / sys.argv[3] / "configs.json").read_text())["configs"]
    profile = next(p for p in profiles if p["id"] == int(sys.argv[4]))
    if record["schema_version"] != 1 or record["status"] != "built" or record["profile"] != profile:
        raise ValueError("tool build record differs from the selected configuration")
    reference = sys.argv[5] + ":" + profile["guestTests"]["compileContainer"]
    image_id = subprocess.check_output(
        ["docker", "image", "inspect", "--format", "{{.Id}}", reference], text=True).strip()
    if record["container"] != {"reference": reference, "image_id": image_id}:
        raise ValueError("tool build container differs from the selected configuration")
    simulator = root / sys.argv[6]
    kernel = root / sys.argv[7]
    for name, repo, paths in (
        ("simulator", simulator, ["eVSSIM/osc-osd", "eVSSIM/scripts/exofs"]),
        ("open-osd", root / "open-osd", []),
        ("kernel", kernel, []),
    ):
        revision = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
        if record["sources"][name] != revision:
            raise ValueError(name + " revision differs from the tool build")
        subprocess.run(["git", "-C", str(repo), "diff", "--quiet", revision, "--"] + paths, check=True)
    recipe = simulator / "infra/builder/compile-exofs-tools.sh"
    if record["recipe_sha256"] != hashlib.sha256(recipe.read_bytes()).hexdigest():
        raise ValueError("exofs tool build recipe changed")
    files = {}
    for name in ("osc-osd", "exofs", "mkfs.exofs", "libosd.so"):
        base = dist / name
        for path in sorted(base.rglob("*")) if base.is_dir() else [base]:
            if path.is_file():
                files[path.relative_to(dist).as_posix()] = {
                    "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                    "size": path.stat().st_size,
                    "mode": path.stat().st_mode & 0o777,
                }
    required = {"osc-osd/up", "osc-osd/tgt/usr/otgtd", "osc-osd/tgt/usr/tgtadm",
                "mkfs.exofs", "libosd.so", "exofs/run_osd_emulator_and_mount_exofs.sh"}
    if (files != record["files"] or not required <= files.keys()
            or any(files[name]["size"] == 0 for name in required)):
        raise ValueError("exofs outputs are missing or differ from the tool build checksums")
except (OSError, ValueError, KeyError, TypeError, StopIteration, subprocess.CalledProcessError) as error:
    print("ERROR Exofs tool validation failed: " + str(error), file=sys.stderr)
    sys.exit(1)
print("INFO Exofs tool inputs and staged outputs verified")
PY
}

exofs_test() {
    verify_exofs_tools

    # Make a fresh copy
    evssim_qemu_fresh_image "$version"

    # Run qemu with test specific configuration
    EVSSIM_RUNTIME_STORAGE_STRATEGY=2 EVSSIM_QEMU_SIMULATOR_ENABLED=yes evssim_qemu_detached "$version"

    # Run tests inside the guest
    echo "INFO Running exofs test"
    local output_dir="/tmp/output"
    local test_rc=0 collection_rc=0 flush_rc=0 stop_rc=0
    # Capture each required result explicitly, so an earlier error does not
    # skip the remaining collection/cleanup commands under builder.sh's set -e.
    evssim_guest "sudo OUTPUT_DIR=$output_dir ./exofs/run_osd_emulator_and_mount_exofs.sh" || test_rc=$?

    # When debugging you can find trace logs at OUTPUT_DIR
    evssim_copy_from_guest "$output_dir" "$output_dir" || collection_rc=$?
    # Stop qemu and wait
    evssim_qemu_flush_disk || flush_rc=$?
    evssim_qemu_stop || stop_rc=$?

    echo "INFO Exofs results: test=$test_rc collection=$collection_rc flush=$flush_rc stop=$stop_rc"
    # Preserve the primary test failure; otherwise report the first required
    # collection/cleanup failure. A fully successful run returns zero.
    local result
    for result in "$test_rc" "$collection_rc" "$flush_rc" "$stop_rc"; do
        if [ "$result" -ne 0 ]; then
            echo "ERROR Exofs run failed (exit $result)"
            return "$result"
        fi
    done
    return 0
}

exofs_test "$@"
