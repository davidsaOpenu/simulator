# QEMU artifact inputs, build, publication, and reuse — S2

`qemu-artifact.py` checks a resolved request and prints an identity record
as JSON. With `--build-dir`, it also performs an isolated full clean QEMU 11
build using the regular-file preparation helper. With `--artifact-root` and
`--dist-root`, it selects a validated matching bundle before compilation or
builds and publishes one on a miss, then returns explicit paths for callers.

Run on the builder host with Python 3, Git, and access to the local Docker
image. No Python packages are required:

```sh
python3 infra/builder/qemu-artifact.py \
    --request /path/to/build-inputs.json \
    --qemu-dir /path/to/qemu \
    --simulator-dir /path/to/simulator
```

## Request

The caller supplies these fields explicitly. The helper does not source
`env.sh`, select a default profile, resolve a Gerrit review's latest patch
set, or fetch/apply dependencies. Profile selection remains with its owner;
this contract accepts the full effective profile after any approved overrides.

| Field | Meaning |
| --- | --- |
| `sources.qemu` / `sources.simulator` | Each contains `revision` (full lowercase commit SHA-1) and `target_branch` (declared delivery branch, not the development checkout's branch name) |
| `profile` | Complete selected `configs.json` object, including `id`, all five component sections, and effective overrides; retain additional configuration fields |
| `container_image` | Local Docker tag, digest, or image ID to inspect; the resulting immutable ID and platform identify the build environment |
| `target` | QEMU target-list value, such as `x86_64-softmmu` |
| `configure_args` | Ordered array of individual configure arguments, excluding `--target-list`; preserve quoting as argument boundaries, without shell evaluation |
| `build_environment` | Explicit environment overrides for the build, such as `CC`, `CFLAGS`, or `LDFLAGS`; `{}` means no overrides beyond the pinned image defaults |
| `dependencies` | Array, possibly empty, of the declared reviews and their actual applied revisions |

Each dependency has exactly these fields:

```json
{
  "source": "qemu",
  "url": "https://review.gerrithub.io/c/davidsaOpenu/qemu/+/1245482",
  "patch_set": 7,
  "revision": "c2efa2978302f8afa8e2deb6ae3064d28605ec2e",
  "applied_revision": "c2efa2978302f8afa8e2deb6ae3064d28605ec2e",
  "target_branch": "11.0"
}
```

`source` is `qemu` or `simulator`. After a cherry-pick, `applied_revision`
can differ from the declared review's `revision`; both are retained. The
helper verifies that the applied commit is an ancestor of the recorded
source HEAD. It does not establish equivalence between arbitrary declared
and applied commits; the dependency application path must establish that
mapping. Dependency and effective-profile target branches must agree with
the corresponding requested source branch.

The source paths must be repository roots at the requested revisions, with
no tracked index/worktree changes or modified submodules. Untracked files
are excluded because later materialization must use the recorded Git trees,
not a recursive copy of the working directory. The identity records each
source tree and its direct gitlink paths/commit IDs. Gitlink commits also pin
their nested trees. Identity-only mode initializes or fetches no submodules.
The x86 build recipe below uses tracked firmware binaries rather than
rebuilding firmware from ROM-source gitlinks.
QEMU's committed regular `VERSION` file supplies `sources.qemu.version`;
this value, rather than an artifact's own claim, determines the expected
executable version during reuse.

The simulator revision must contain the identity helper itself and the
existing build/preparation/wrapper recipes. Their tracked regular-file modes
and SHA-256 content hashes are recorded; the running helper must match its
recorded recipe. This prevents a changed, unrecorded helper from silently
describing an older simulator revision. An uncommitted development version
can be tested with isolated fixture repositories, but cannot identify itself
as part of a committed production source revision.

## Identity and handoff

The output contains:

- `inputs`: schema version 1, actual source revisions/trees/gitlink pins,
  effective profile, inspected image ID/platform, target, ordered configure
  arguments, build environment, recipe hashes/modes, and dependency records.
- `key`: SHA-256 of canonical `inputs` JSON: sorted object keys, compact
  separators, ASCII escapes, no trailing newline, and no non-finite numbers.
  Dependencies are sorted by URL; argument order is preserved.
- `container_reference`: the requested tag/digest, for diagnostics. A different
  tag pointing to the same image does not change the key. Later builds must
  run the recorded immutable image ID rather than resolve the tag again.
- `bundle_relative_path`: `qemu/<key>` and `manifest_relative_path`:
  `qemu/<key>/manifest.json`. Resolve these beneath the persistent `artifacts/`
  root or the selected `dist/` root in later steps. Absolute checkout paths
  and timestamps do not affect identity.

Any relevant recorded input change produces a different key. Declared review
metadata is included conservatively, so a new patch-set record also changes
the key even if its compiled content is unchanged.

Exit zero means all input checks succeeded and stdout contains one JSON
record. Failure returns nonzero, writes an explanation to stderr, and emits
no stdout record. Unknown request fields, duplicate JSON fields, incomplete
profiles, mutable/malformed image IDs, mismatched source revisions, dirty
tracked sources, and unapplied dependencies are rejected.

The record is **not a successful-build manifest**. A path in it is the
expected location for a future bundle, not evidence that an executable
exists. Later steps must validate bundle contents and build success before
any consumer treats it as runnable. They must also materialize the recorded
source revisions and apply only the recorded build environment and options.

## Source preparation — step 2

The existing two-argument invocation remains:

```sh
bash infra/builder/prepare-qemu-vssim.sh /path/to/qemu /path/to/simulator/eVSSIM
```

The caller must supply the authoritative source snapshot selected for the
build. Step 3 materializes that recorded revision in the disposable build
workspace. The helper copies only its explicitly listed files: the existing
flattened SSD/FTL/configuration/logging inputs, required OSD sources/headers
with their relative paths, the SQL schema and license files, and the monitor's
`index.html`, `index.css`, and `favicon.ico`. It generates `osd-schema.c` with
the existing transformation. No QEMU Meson path or simulator behavior changes.

Every staged source is a regular-file copy. Required source-file or parent
directory symlinks are rejected; unlisted outputs and links are not traversed.
The OSD `tgt` tree, object files, and unrelated monitor outputs are excluded.
Source edits remain under authoritative `eVSSIM/`; rerun preparation to refresh
the generated copies after an edit.

Preparation uses a temporary sibling directory and replaces the generated
`hw/vssim/simulator/` view only after all copies and schema generation succeed.
Missing inputs or copy/schema failures return nonzero, remove temporary files,
and leave a previous prepared view intact. Callers must honor that failure
instead of continuing with the previous view. A successful refresh removes
stale generated files. The existing Ubuntu 26.04 build path already invokes
this helper after cleanup; legacy build selection is unchanged.

## Isolated full clean build — step 3

Use the same request with a **new, nonexistent** job directory. Its parent
must already exist. The following explicit mode runs before `builder.sh`
is sourced and works from any current directory:

```sh
bash /path/to/simulator/infra/builder/compile-qemu.sh \
    --request /path/to/build-inputs.json \
    --qemu-dir /path/to/qemu \
    --simulator-dir /path/to/simulator \
    --build-dir /tmp/qemu-build-job \
    --subproject-cache /path/to/qemu/subprojects \
    --jobs 4
```

This mode currently supports `11.0`, `ubuntu-26.04`, and
`x86_64-softmmu`. The request must include `--enable-werror` and
`--disable-download`, without overrides disabling warnings-as-errors or
enabling downloads. The default version-based invocation remains unchanged,
including its legacy QEMU and exofs steps. Existing CI callers will switch
to the new input/output handoff in the remaining S2 work; they do not select
this mode automatically yet.

The helper exports both recorded source commits from Git objects into
`workspace/`, excluding untracked files, local outputs, and Git metadata.
The x86 recipe uses QEMU's tracked `pc-bios/` firmware binaries. It does not
build the unrelated firmware-source or test-tool gitlinks, nor initialize
them based on whichever modules happen to be present in the checkout.

The four required Meson wrap-git projects (`dtc`, `keycodemapdb`,
`berkeley-softfloat-3`, `berkeley-testfloat-3`) must be available as local Git
repositories beneath `--subproject-cache` (default: the source QEMU's
`subprojects/`). Their immutable revisions come from the recorded QEMU wrap
files. Only those Git objects are exported, and QEMU's tracked
`packagefiles/` overlays are applied. No cached object files or working-tree
edits are copied. Missing pinned objects fail before container execution.
The source-cache location and job count do not change the input key; they
do not select source contents. Dependency downloads/population are separate
from this build operation.

Preparation, configure, Ninja, and the executable's `--version` each run in
the recorded immutable Docker image with only the job workspace mounted.
The container runs as the calling UID/GID, overrides the image's VM-checking
entrypoint with `/usr/bin/env`, and requires no privileged mode or KVM device
for compilation. Recorded environment overrides and argument arrays pass
literally, without shell evaluation. The pinned image defaults remain the
base environment. Existing configure-time Python dependency installation
may still require network access; `--disable-download` governs QEMU/Meson
source dependencies.

A successful call retains `workspace/qemu/` with the build and tracked
firmware, writes `inputs.json`, per-stage `logs/*.log`, `logs/commands.json`
(argument arrays and exit statuses), and `build-result.json`, then prints
the build result as one JSON record. This is an intermediate build handoff,
**not a published reusable bundle or runtime acceptance**. The caller owns
removing the successful workspace after the next step consumes it.

Any export, source preparation, configure, build, or version-command failure
returns nonzero with no stdout result, removes the incomplete workspace,
and retains the inputs/logs available for diagnosis. An existing job
directory is rejected and never cleaned or reused. Development repositories
are never checked out, reset, cleaned, or updated by this mode.

## Bundle publication — step 4

Add both roots to the explicit build invocation:

```sh
bash /path/to/simulator/infra/builder/compile-qemu.sh \
    --request /path/to/build-inputs.json \
    --qemu-dir /path/to/qemu --simulator-dir /path/to/simulator \
    --build-dir /tmp/new-qemu-build-job --jobs 4 \
    --subproject-cache /path/to/subproject-git-repositories \
    --artifact-root "$EVSSIM_ROOT_PATH/artifacts" \
    --dist-root "$EVSSIM_ROOT_PATH/$EVSSIM_DIST_FOLDER"
```

The caller supplies the effective paths; the helper does not source the
legacy environment. The artifact root must be persistent storage and
distinct from the selected `dist` root, which may be tmpfs. Neither root
changes the input key. This invocation now checks persistent storage before
building, as described under step 5 below.

After a successful build, Meson installs into a new directory under the job
workspace with `install --no-rebuild`. Its generated installation list
determines the payload, including tools such as `qemu-img`, firmware, keymaps,
and generated firmware data. Every listed installed file must exist as a
regular file. Installation targets outside the configured prefix, absolute
`bindir`/`datadir` values, and payload symlinks are rejected rather than
silently omitted. The supported default layout is `bin/` and `share/qemu/`.
The installation prefix itself is stripped when constructing the bundle.

Each bundle contains the installed payload plus `manifest.json`, which
records schema version, input key, exact inputs, QEMU version, executable
and data paths relative to the bundle, full-clean-build success, and every
payload file's SHA-256, byte size, and permission mode. Verification requires
the exact file inventory: missing, changed, extra, linked, or non-regular
files fail. The manifest itself is excluded from its payload inventory.

Before publication, the packaged executable must report the expected QEMU
11.0 version in the recorded image. A paused PC then initializes with the
packaged data directory, loading its BIOS and VGA ROMs, and exits through
the monitor. Required firmware files are also checked explicitly. These
probes mount only the bundle read-only, disable networking, and have a
30-second limit with container cleanup on failure. They do not run the
project's guest tests or establish runtime migration acceptance. Shared
libraries come from the recorded image; this is not a portable host binary.

The persistent candidate is constructed in a temporary sibling on the
artifact filesystem. A per-key advisory lock serializes the final check
and rename into `artifacts/qemu/<key>/`. A concurrent winner must pass full
manifest, payload, version, and firmware validation; it is never overwritten.
An invalid existing entry causes failure and is left intact for diagnosis.
Temporary candidates are removed on every ordinary error.

The persistent bundle is independently copied, validated, and atomically
renamed under the selected `dist/qemu/<key>/`, using the same locking rule.
There are no links back to the build workspace or persistent bundle. A
staging failure returns nonzero without a selection; an already completed,
valid persistent bundle remains available. Atomic visibility does not imply
a transaction spanning two filesystems or power-loss durability.

The publication helper produces the persistent/staged bundle paths; step 5
returns the caller selection below. Build and publication commands/logs
remain in the job's `logs/` directory, including
`publication-commands.json`. Install staging is removed on success or failure;
the successful build workspace remains owned by the caller as in step 3.
After retaining its evidence, the caller can remove it: both bundles contain
independent regular files and remain usable in the recorded image.

## Validated reuse and caller handoff — step 5

The same explicit command now performs lookup before any source export or
compilation. A new job directory is required for each attempt, including a
hit. Source checkouts must still match the request and image inspection must
succeed; a cache hit does not waive input validation.

If `artifacts/qemu/<key>` is absent, the operation performs the existing full
clean build, installation, publication, and staging. It returns `cache: miss`.
An existing entry is held under its per-key lock while its exact manifest,
every payload checksum/size/mode, source `VERSION`, executable version, and
firmware initialization are checked. A valid entry is staged through the
same atomic copy/validation path and returns `cache: hit`. Hits do not call
the compiler, source exporter, preparation helper, configure, Ninja, or
Meson install; no subproject source cache is required on that path.

Corrupt or incomplete persistent entries fail with a nonzero exit and stay
intact for diagnosis. They are not silently accepted, deleted, or rebuilt.
An invalid existing staged entry also fails without a success selection.
A missing staged entry is recreated from the validated persistent bundle.
Repair/removal of invalid entries is an explicit operator action; using a
fresh staging root also avoids a damaged `dist` entry.

Successful stdout is one JSON object with:

| Field | Meaning |
| --- | --- |
| `status`, `cache` | `selected`, and `hit` or `miss` |
| `key` | Exact canonical input key |
| `bundle`, `staged_bundle`, `manifest` | Absolute persistent/staged bundle and staged manifest paths |
| `executable`, `data_dir` | Absolute selected executable and firmware/data directory paths |
| `version`, `binary_sha256` | Verified source/executable version and staged binary hash |
| `container_image_id` | Immutable image that must run the executable |
| `profile_id`, `sources` | Effective configuration ID and actual QEMU/simulator commit IDs; full inputs remain in the manifest |

The caller must check the command's exit status before consuming stdout.
S3's launcher can mount the selected bundle and use the manifest's relative
paths; S7 can record the selection with the workflow results. No implicit
last-built binary, shell `eval`, or source-tree symlink is needed.

On hits, `inputs.json`, `logs/cache.json`, and `logs/cache-commands.json`
remain in the new job; there is no build workspace. On misses, the build
and publication logs remain, with `logs/cache.json` recording the completed
selection or publication failure. Build failures retain the existing build
logs and emit no selection. Successful miss workspaces are still caller-owned.

This implements S2's explicit resolved-input interface for QEMU 11/x86.
Automatic profile selection/request production remains with S1; launcher
and workflow migration remain S3/S7. Existing version-based `compile-qemu.sh`
calls and legacy/exofs work continue on their existing path. The current
`run-ci.sh` does not yet supply this request or consume the JSON selection;
that integration must be exercised before closing S2's overall handoff.

## Checks

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s infra/builder/tests -p 'test_qemu_artifact.py' -v
```

Tests call the production helper with isolated Git repositories and controlled
Docker responses. They check key stability/sensitivity, dependency application,
submodule pins, recipe identity, source rejection, and CLI output/exit status.
Preparation tests also check copy independence/refresh, local include paths,
schema output, assets, excluded outputs, linked/missing input rejection,
replacement of old generated links, and copy/schema failure propagation.
Build tests also exercise committed source exports, pinned source-cache
selection, fresh build directories, literal arguments/environment, the shell
entrypoint, each subprocess failure, and cleanup/log retention. Controlled
container commands are used in unit tests; real full clean build evidence
is recorded separately. These tests do not constitute runtime acceptance.
Publication tests also cover full installed-file inventory, executable modes,
missing/corrupt payloads, bad versions, firmware initialization failures,
copy/install/rename errors, concurrent publishers, timeout cleanup, and a
failed `dist` stage preserving the completed persistent bundle.
Cache tests cover hit/miss selection, skipped compilation, changed-profile
invalidation, explicit handoff paths, missing staging, corrupt entries, and
failure exit status through `compile-qemu.sh`. Run builder tests manually
using the command above. The existing tox unit environment continues to run
only the ELK tests; CI discovery was not changed.
