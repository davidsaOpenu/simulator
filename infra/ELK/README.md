# ELK startup recovery

An interrupted build can leave ELK containers running after Jenkins replaces
their checkout. A bind mount can still reference the deleted directory even
when its recorded source path matches the replacement checkout. A healthy
Kibana response does not prove that Filebeat can read the current logs.

`install_and_start_elk.sh` keeps its existing two arguments: the simulator log
directory and the ELK directory. Both directories must already exist. CI
continues to call this entry point through the existing pipeline.

## Startup behavior

- Noninteractive startup recreates the selected checkout's surviving containers,
  including stopped ones. This also restarts a healthy stack: comparing path
  strings cannot establish whether its mounts survived checkout replacement.
- Compose ownership is checked against the selected project and working
  directory. Standalone Filebeat must use that project's network and this
  checkout's configuration mount. Ownership conflicts fail without deleting
  the conflicting resources.
- Filebeat is removed before Compose teardown so it releases the network.
  Recovery retains persistent volumes and images; it does not purge them.
- Complete TLS files are reused. An incomplete generated certificate set is
  regenerated after stopping the selected stack, since the TLS initializer
  otherwise skips existing keys.
- Required setup failures return nonzero. Before reporting success, startup
  checks that Filebeat is running and can connect to its output. Recreating
  the container establishes fresh mounts; comparing source-path strings cannot
  detect stale mounts. The existing host metrics gate verifies actual ingestion.

Interactive startup retains the existing restart prompt. Declining a restart
checks Kibana health and leaves the running stack in place.

The recovery change is confined to startup. Pipeline stages, test workloads,
metric thresholds, QEMU configuration and the general cleanup script are
unchanged. A noninteractive invocation takes the normal startup path rather
than the previous early return for an already-running stack.

## Tests and validation

From the simulator repository root, run the maintained ELK tests with:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s infra/ELK -p 'test_*.py' -v
```

`test_elk_startup.py` invokes the installer with isolated temporary checkouts
and controlled external commands. It covers checkout replacement, stopped
survivors, partial TLS, ownership conflicts, setup failures, launch mount
arguments and repeated startup. A pseudo-terminal test checks that declining
an interactive restart leaves containers untouched under Docker and Podman.
These tests do not contact a real container daemon.

The implementation published as patch set 1 was validated on 2026-11-15.
All 46 ELK tests passed. A disposable VM reproduced the original stale Docker
mount and verified corrected Docker and rootless Podman recovery, ingestion
from replacement checkouts, repeated Docker recovery with a missing
certificate, and retained volume identities.
The full unchanged host metrics gate passed all seven simulation groups and
both event-generator cases. That run reused the matching simulation binary
and builder image; it was not a fresh compilation or a Jenkins acceptance run.

After simplifying the startup path on 2026-11-17, all 46 ELK tests passed again,
including the interactive-decline test. The full VM validation was not repeated
for that simplification.
