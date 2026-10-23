# QEMU artifact inputs, preparation, and build — S2 steps 1–3

`qemu-artifact.py` checks a resolved request and prints an identity record
as JSON. With `--build-dir`, it also performs an isolated full clean QEMU 11
build using the regular-file preparation helper. Publication, cache reuse,
and workflow handoff remain steps 4–5.

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
build. Step 3 will materialize that recorded revision in the disposable build
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

## Checks within steps 1–3

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
