# Standalone exofs guest test

Builds and runs an exofs-over-NVMe test that is independent of the shared
eVSSIM infrastructure: the regular flow (kernel 5.0.0, `dist/`, `data/`,
`~/kernel`, `~/qemu`, configs.json, run-ci.sh, open-osd) is not touched.
Everything this test produces lives in its own folders next to them:

| folder | content |
|---|---|
| `~/kernel-exofs` | git worktree of the kernel repo at the fixed commit of CR 1245198 (7.0.2 + exofs) |
| `~/qemu-exofs` | git worktree of the qemu repo at the fixed commit of CR 1245496, own build |
| `~/dist-exofs` | kernel, initrd, modules, nvme, mkfs.exofs, guest image |
| `~/data-exofs` | simulator runtime data of the test VM |
| `~/logs/exofs` | build/test logs, `STATUS`, test results |

One command does everything (skips what is already built):

```bash
cd ~/simulator/infra/builder && ./exofs/all.sh
```

```
build.sh  1. kernel 7.0.2 + exofs.ko + initrd     -> dist-exofs/kernel/
             nvme-cli + mkfs.exofs (14.04 glibc)   -> dist-exofs/{nvme,mkfs.exofs}
             Ubuntu 14.04 image with all of above  -> dist-exofs/system.img.template
run.sh    2. boot the image on the project qemu (object strategy, 2 NVMe drives)
          3. nvme set-feature <dev> -f 192 --value=1   (key/value mode)
          4. insmod exofs.ko
          5. mkfs.exofs + mount -t exofs
          6. exofs test suite (eVSSIM/tests/guest/exofs), results -> logs/exofs/<date>/
```

## Prerequisites

* docker images: `./build-docker-image.sh` (needs `evssim:ubuntu-14.04` and `evssim:ubuntu-26.04`)
* kernel and qemu are built from **fixed commits** (set in `env-exofs.sh`,
  no cherry-pick): `~/kernel-exofs` is checked out at `EXOFS_KERNEL_COMMIT`
  (CR 1245198 patch set 1, kernel 7.0.2 + exofs) and `~/qemu-exofs` at
  `EXOFS_QEMU_COMMIT` (CR 1245496 patch set 3, on top of qemu master). A
  commit missing in the local repo is fetched from its Gerrit ref; changing
  a commit makes the next `all.sh` rebuild that step. The shared `~/kernel`
  and `~/qemu` checkouts are not touched.
* `versions/kernel/7.0.2/.config` - until CR 1237773 is merged the file may be
  missing; `build.sh kernel` then takes it from the fixed ref
  `EXOFS_KERNEL_CONFIG_REF` (patch set 19) into `~/dist-exofs` (`versions/`
  is not touched; `EXOFS_KERNEL_CONFIG_REF=` disables this).
* simulator with "Fix FTL page aligned and offset obj read" and the mkfs.exofs CR

## Usage (from `infra/builder`)

```bash
./exofs/build.sh                 # qemu + kernel + tools + image (first time: long)
./exofs/run.sh boot              # only check that the VM boots 7.0.2 and sees the drives
./exofs/run.sh shell             # boot and get a shell in the guest (manual debugging)
./exofs/run.sh                   # full test suite
```

Useful variables:

| variable | default | meaning |
|---|---|---|
| `EXOFS_KERNEL_FOLDER` | `kernel-exofs` | kernel tree (worktree created automatically); `kernel` reuses the existing tree |
| `EXOFS_KERNEL_COMMIT` / `_REF` | CR 1245198 ps1 | fixed kernel commit and the Gerrit ref it is fetched from |
| `EXOFS_QEMU_COMMIT` / `_REF` | CR 1245496 ps3 | fixed qemu commit and the Gerrit ref it is fetched from |
| `EXOFS_BASE_IMAGE` | – | start from an existing pristine 14.04 image instead of downloading one |
| `EXOFS_STRICT` | `0` | `1` also runs the tests of known exofs issues |
| `EXOFS_TESTS` | all | e.g. `test_02_exofs_api.py:TestRename` |
| `EXOFS_LARGE_FILE_MB` | `8` | size of the large-file test |
| `EXOFS_MANY_FILES` | `200` | files in the many-files test |

Steps can be combined: `./exofs/build.sh kernel image`.

## Inside the guest

```bash
sudo ~/guest/exofs/run_exofs_tests.sh                              # everything
sudo ~/guest/exofs/run_exofs_tests.sh test_02_exofs_api.py:TestLinks
sudo EXOFS_STRICT=1 ~/guest/exofs/run_exofs_tests.sh               # incl. known issues
```

## Known exofs issues (tests marked `@known_issue`, skipped unless `EXOFS_STRICT=1`)

* `exofs_write_begin()` treats the positive byte count returned by
  `nvme_submit_sync_kv_read()` as an error (`if (ret)`) and returns it with
  `folio == NULL`: oops on any write into an existing page that is not cached
  (e.g. append after remount). Fix: `if (ret < 0)`.
* truncate does not resize the KV object (TODO in `exofs_setattr()`).
* `exofs_mkdir()` leaves new directories with `nlink == 1`.
* `exofs_rename()` of a directory to another parent does not update `..` /
  parent link counts, and replacing a non-empty directory is not rejected.
* `exofs_tmpfile()` does not call `finish_open_simple()` (O_TMPFILE).
* `exofs_show_options()` prints `TODO\n` into `/proc/mounts`.
* `exofs_new_inode()` never clears `ei->i_data` (slab memory is reused), so a
  slow symlink (target > 19 bytes) persists stale bytes and is read back as a
  fast symlink after remount / drop_caches (flaky). Fix:
  `memset(ei->i_data, 0, sizeof(ei->i_data));` in `exofs_new_inode()`.
* `exofs_rename()` over an existing name always fails with `EINVAL`:
  `exofs_set_link()` requires a locked folio, but the one from
  `exofs_find_entry()` is unlocked. Fix: `folio_lock()` / `folio_unlock()`
  around `exofs_set_link()` (as `exofs_delete_entry()` does).

## Known qemu / environment issues

* qemu KV mode (`nvme_kv_io_cmd()` in `hw/block/nvme.c`) rejects NVMe Flush
  with Invalid Opcode although the controller reports a volatile write cache
  (`vwc = 1`), so every `fsync()` fails with `EOPNOTSUPP`. The tests set
  `/sys/block/nvmeXn1/queue/write_cache` to `write through` after enabling KV
  mode (`EXOFS_KEEP_WRITE_CACHE=1` keeps it; the known-issue test
  `TestKnownQemuIssues` covers it). Fix: handle `NVME_CMD_FLUSH` in
  `nvme_kv_io_cmd()` (return success - objects are stored synchronously).
* The initrd is built with the 26.04 udev, which renames `eth0` to `enp0s5`;
  the 14.04 guest only configures `eth0`. `kernel.config` therefore adds a
  built-in command line `net.ifnames=0 biosdevname=0`.
* The regular VM (`--net=host`) holds ports 2222, 2003 and 5900, so this test
  uses ssh 2223 and VNC :7. The simulator's log server inside our qemu cannot
  bind 2003 while the regular VM runs; it is not needed by the tests.
