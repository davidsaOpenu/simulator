#!/bin/bash
set -euo pipefail

# Config #4 runs QEMU in 26.04 but keeps the existing 14.04 guest/tool ABI.
# This helper builds only the exofs payload; it does not source builder.sh,
# check out repositories, or compile QEMU/kernel modules.
if [[ $# -ne 1 || "${EVSSIM_ENVIRONMENT:-}" != yes ]]; then
    echo "Usage: source ./env.sh; $0 <ubuntu-14.04>" >&2
    exit 1
fi
version="$1"
if [[ "$version" != ubuntu-14.04 || "$version" != "${EVSSIM_GUEST_TESTS_COMPILE_CONTAINER:-}" ]]; then
    echo "ERROR Exofs tools require the configured ubuntu-14.04 guest-tool container" >&2
    exit 1
fi

root="$EVSSIM_ROOT_PATH"
simulator="$root/$EVSSIM_SIMULATOR_FOLDER"
kernel="$root/$EVSSIM_KERNEL_FOLDER"
open_osd="$root/open-osd"
dist="$root/$EVSSIM_DIST_FOLDER"
image_reference="$EVSSIM_DOCKER_IMAGE_NAME:$version"
# Resolve once: a tag change between the two make invocations must not change
# the compiler or libraries used for this payload.
image_id=$(docker image inspect --format '{{.Id}}' "$image_reference")
simulator_revision=$(git -C "$simulator" rev-parse HEAD)
kernel_revision=$(git -C "$kernel" rev-parse HEAD)
open_osd_revision=$(git -C "$open_osd" rev-parse HEAD)
if ! git -C "$simulator" diff --quiet "$simulator_revision" -- eVSSIM/osc-osd eVSSIM/scripts/exofs ||
        ! git -C "$kernel" diff --quiet "$kernel_revision" ||
        ! git -C "$open_osd" diff --quiet "$open_osd_revision"; then
    echo "ERROR Exofs source inputs have uncommitted changes; no build was started" >&2
    exit 1
fi

mkdir -p "$root/$EVSSIM_LOGS_FOLDER" "$dist"
job=$(mktemp -d "$root/$EVSSIM_LOGS_FOLDER/exofs-tools.XXXXXX")
workspace="$job/workspace"
mkdir -p "$workspace/simulator" "$workspace/kernel" "$workspace/open-osd" "$workspace/staged"
# Only this invocation's disposable sources are removed. Build output remains
# in build.log on success and failure; development checkouts stay untouched.
trap 'rm -rf -- "$workspace"' EXIT
jq -e --argjson id "$EVSSIM_VERSIONS_CONFIGURATION_ID" \
    '.configs[] | select(.id == $id)' \
    "$root/$EVSSIM_VERSIONS_FOLDER/configs.json" > "$job/profile.json"
git -C "$simulator" archive "$simulator_revision" -- eVSSIM/osc-osd eVSSIM/scripts/exofs |
    tar -xf - -C "$workspace/simulator"
git -C "$kernel" archive "$kernel_revision" | tar -xf - -C "$workspace/kernel"
git -C "$open_osd" archive "$open_osd_revision" | tar -xf - -C "$workspace/open-osd"

echo "INFO Exofs tool inputs: simulator=$simulator_revision open-osd=$open_osd_revision kernel=$kernel_revision image=$image_id"
echo "INFO Exofs tool build log: $job/build.log"
# Use the existing make arguments in fresh exports. Bypass the normal image
# entrypoint because this userspace build needs neither KVM nor a guest image.
if ! docker run --rm --user "$(id -u):$(id -g)" \
    --volume "$workspace:/work" --workdir /work --entrypoint /bin/bash \
    "$image_id" -euo pipefail -c '
        cd /work/simulator/eVSSIM/osc-osd
        make MK_PATH=. ARCH=x86_64 clean
        make MK_PATH=. ARCH=x86_64 -j"$1"
        rsync -a --exclude=.git ./ /work/staged/osc-osd/

        cd /work/open-osd
        make KSRC=/work/kernel ARCH=x86_64 clean
        make KSRC=/work/kernel -j"$1"
        cp usr/mkfs.exofs /work/staged/mkfs.exofs
        cp lib/libosd.so /work/staged/libosd.so
        cp -r /work/simulator/eVSSIM/scripts/exofs /work/staged/exofs

        for file in osc-osd/up osc-osd/tgt/usr/otgtd osc-osd/tgt/usr/tgtadm \
                    mkfs.exofs libosd.so exofs/run_osd_emulator_and_mount_exofs.sh; do
            test -s "/work/staged/$file"
        done
    ' bash "$(nproc)" > "$job/build.log" 2>&1; then
    echo "ERROR Exofs tool build/staging failed; see $job/build.log" >&2
    exit 1
fi

# The record describes this successful tool build, not a new artifact cache.
# Hash all staged files so the exofs wrapper can reject incomplete/mixed outputs.
python3 - "$job" "$workspace/staged" "$simulator_revision" "$open_osd_revision" \
    "$kernel_revision" "$image_reference" "$image_id" "${BASH_SOURCE[0]}" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

job, staged = map(Path, sys.argv[1:3])
files = {}
for path in sorted(staged.rglob("*")):
    if path.is_file():
        files[path.relative_to(staged).as_posix()] = {
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "size": path.stat().st_size,
            "mode": path.stat().st_mode & 0o777,
        }
record = {
    "schema_version": 1,
    "status": "built",
    "profile": json.loads((job / "profile.json").read_text()),
    "sources": dict(zip(("simulator", "open-osd", "kernel"), sys.argv[3:6])),
    "container": {"reference": sys.argv[6], "image_id": sys.argv[7]},
    "recipe_sha256": hashlib.sha256(Path(sys.argv[8]).read_bytes()).hexdigest(),
    "files": files,
}
(job / "exofs-tools.json").write_text(json.dumps(record, indent=2) + "\n")
PY

# Publish only after every build/output check succeeds. Invalidate the old
# record before replacing files, and install the new record last so a failed
# copy cannot leave partial outputs labelled as a successful matching build.
rm -f -- "$dist/exofs-tools.json"
rm -rf -- "$dist/osc-osd" "$dist/exofs"
cp -a "$workspace/staged/osc-osd" "$workspace/staged/exofs" "$dist/"
cp -p "$workspace/staged/mkfs.exofs" "$workspace/staged/libosd.so" "$dist/"
cp "$job/exofs-tools.json" "$dist/.exofs-tools.json.tmp"
mv -- "$dist/.exofs-tools.json.tmp" "$dist/exofs-tools.json"
echo "INFO Exofs tools staged in $dist; inputs/checksums: $dist/exofs-tools.json"
