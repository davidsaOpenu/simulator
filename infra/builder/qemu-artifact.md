# QEMU artifact inputs — S2 step 1

`qemu-artifact.py` defines the input contract for S2's remaining work. It
checks a resolved request and prints an identity record as JSON. It does
not compile QEMU, copy simulator sources, publish a bundle, or accept a
cache hit. Those operations remain steps 2–5 of the S2 plan.

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
their nested trees; recursively materializing/verifying those pinned trees
belongs to step 3. No submodule is initialized or fetched here.

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

## Checks within step 1

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s infra/builder/tests -p 'test_qemu_artifact.py' -v
```

Tests call the production helper with isolated Git repositories and controlled
Docker responses. They check key stability/sensitivity, dependency application,
submodule pins, recipe identity, source rejection, and CLI output/exit status.
They do not compile QEMU or constitute runtime acceptance.
