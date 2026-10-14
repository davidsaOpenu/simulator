#!/usr/bin/env python
"""
exofs API test suite (step 6 of the test plan).

Runs against a freshly formatted exofs mounted on EXOFS_DEVICE. Every test
works inside its own directory and most tests verify their result twice:
once through the page cache and once after a full unmount/mount cycle, so
the data really went through exofs -> NVMe key/value -> FTL and back.

Coverage (exofs operations in brackets):
  files       create/open/read/write/seek/fsync, sizes around page
              boundaries, large files, sparse files, overwrite, truncate,
              mmap, O_APPEND, O_EXCL, O_DIRECT, concurrency
              [create, lookup, write_begin/write_end, writepages,
               read_folio, setattr, evict_inode]
  dirs        mkdir/rmdir/readdir, nesting, many entries over several
              directory pages, error cases [mkdir, rmdir, iterate_shared,
              add_link, delete_entry, empty_dir]
  names       max length, ENAMETOOLONG, special characters
  rename      same dir, across dirs, replace, directories [rename]
  links       hard links, fast and slow symlinks [link, symlink]
  special     fifo, char/block device nodes [mknod]
  metadata    chmod/chown/utime/timestamps/statfs [setattr, write_inode,
              statfs]
  persistence full tree manifest across remounts

Tests of issues already found by code review are marked with
@known_issue and are skipped unless EXOFS_STRICT=1.
"""

from __future__ import print_function

import errno
import mmap
import os
import stat
import threading
import time

import exofs_helpers as h
from exofs_helpers import PAGE


def setup_module():
    h.bring_up(fresh=True)


def teardown_module():
    h.tear_down()


# ---------------------------------------------------------------------------
# Files: create / open / read / write
# ---------------------------------------------------------------------------
class TestFileBasics(h.ExofsTestCase):

    def test_create_empty_file(self):
        path = self.path("empty")
        open(path, "wb").close()
        st = os.stat(path)
        self.assertTrue(stat.S_ISREG(st.st_mode))
        self.assertEqual(st.st_size, 0)
        self.assertEqual(st.st_nlink, 1)
        self.remount()
        self.assertEqual(os.stat(path).st_size, 0)
        self.assertEqual(h.read_file(path), b"")

    def test_create_with_mode(self):
        path = self.path("mode")
        fd = os.open(path, os.O_CREAT | os.O_WRONLY, 0o640)
        os.close(fd)
        self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o640 & ~self._umask())
        self.remount()
        self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o640 & ~self._umask())

    @staticmethod
    def _umask():
        current = os.umask(0)
        os.umask(current)
        return current

    def test_open_missing_file(self):
        h.assert_errno(errno.ENOENT, os.open, self.path("missing"), os.O_RDONLY)

    def test_open_missing_parent(self):
        h.assert_errno(errno.ENOENT, os.open, self.path("no-dir", "file"), os.O_CREAT | os.O_WRONLY)

    def test_o_excl_on_existing(self):
        path = self.path("excl")
        os.close(os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644))
        h.assert_errno(errno.EEXIST, os.open, path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)

    def test_o_creat_existing_keeps_data(self):
        path = self.path("keep")
        h.write_file(path, b"keep me")
        os.close(os.open(path, os.O_CREAT | os.O_WRONLY, 0o644))
        self.assertEqual(h.read_file(path), b"keep me")

    def test_o_trunc(self):
        path = self.path("trunc")
        h.write_file(path, h.pattern(3 * PAGE))
        os.close(os.open(path, os.O_WRONLY | os.O_TRUNC))
        self.assertEqual(os.stat(path).st_size, 0)
        self.remount()
        self.assertEqual(os.stat(path).st_size, 0)

    def test_open_directory_for_write_fails(self):
        h.assert_errno(errno.EISDIR, os.open, self.dir, os.O_WRONLY)

    def test_write_on_readonly_fd_fails(self):
        path = self.path("ro")
        h.write_file(path, b"data")
        fd = os.open(path, os.O_RDONLY)
        try:
            h.assert_errno(errno.EBADF, os.write, fd, b"x")
        finally:
            os.close(fd)

    def test_read_past_eof(self):
        path = self.path("eof")
        h.write_file(path, b"0123456789")
        fd = os.open(path, os.O_RDONLY)
        try:
            os.lseek(fd, 100, os.SEEK_SET)
            self.assertEqual(os.read(fd, 10), b"")
            os.lseek(fd, 5, os.SEEK_SET)
            self.assertEqual(os.read(fd, 100), b"56789")
        finally:
            os.close(fd)

    def test_lseek(self):
        path = self.path("seek")
        h.write_file(path, h.pattern(PAGE + 123))
        fd = os.open(path, os.O_RDONLY)
        try:
            self.assertEqual(os.lseek(fd, 0, os.SEEK_END), PAGE + 123)
            self.assertEqual(os.lseek(fd, -23, os.SEEK_END), PAGE + 100)
            self.assertEqual(os.lseek(fd, 10, os.SEEK_CUR), PAGE + 110)
            h.assert_errno(errno.EINVAL, os.lseek, fd, -1, os.SEEK_SET)
        finally:
            os.close(fd)

    def test_fsync_and_fdatasync(self):
        path = self.path("sync")
        fd = os.open(path, os.O_CREAT | os.O_WRONLY, 0o644)
        try:
            os.write(fd, h.pattern(5000))
            os.fsync(fd)
            os.fdatasync(fd)
        finally:
            os.close(fd)
        self.remount()
        h.assert_same_data(h.pattern(5000), h.read_file(path), "fsync")

    def test_o_direct_is_rejected_cleanly(self):
        # exofs has no direct_IO, the VFS must refuse O_DIRECT with EINVAL
        path = self.path("direct")
        h.write_file(path, b"x")
        h.assert_errno(errno.EINVAL, os.open, path, os.O_RDWR | getattr(os, "O_DIRECT", 0o40000))

    def test_unlink(self):
        path = self.path("gone")
        h.write_file(path, b"bye")
        os.unlink(path)
        self.assertFalse(os.path.lexists(path))
        h.assert_errno(errno.ENOENT, os.unlink, path)
        self.remount()
        self.assertFalse(os.path.lexists(path))

    def test_unlink_open_file_keeps_data_until_close(self):
        path = self.path("orphan")
        data = h.pattern(2 * PAGE + 7)
        h.write_file(path, data)
        fd = os.open(path, os.O_RDONLY)
        try:
            os.unlink(path)
            self.assertFalse(os.path.lexists(path))
            self.assertEqual(os.fstat(fd).st_nlink, 0)
            h.assert_same_data(data, os.read(fd, len(data) + 10), "read of unlinked open file")
        finally:
            os.close(fd)
        self.remount()
        self.assertEqual(os.listdir(self.dir), [])

    def test_file_count_tracks_create_and_unlink(self):
        before = h.statvfs_files()
        for i in range(5):
            open(self.path("f%d" % i), "wb").close()
        self.assertEqual(h.statvfs_files(), before + 5)
        for i in range(5):
            os.unlink(self.path("f%d" % i))
        self.assertTrue(h.wait_for(lambda: h.statvfs_files() == before),
                        "f_files is %d after unlinking, expected %d" % (h.statvfs_files(), before))
        self.remount()
        self.assertEqual(h.statvfs_files(), before, "f_files not persisted in the superblock")

    def test_statfs_magic(self):
        self.assertEqual(h.fs_magic(self.dir), h.EXOFS_SUPER_MAGIC)


# ---------------------------------------------------------------------------
# Files: data sizes and page boundaries
# ---------------------------------------------------------------------------
SIZES = [1, 2, 100, 511, 512, 513, PAGE - 1, PAGE, PAGE + 1, 2 * PAGE - 1, 2 * PAGE,
         2 * PAGE + 1, 3 * PAGE + 17, 16 * PAGE, 64 * 1024 + 3, 256 * 1024, 1024 * 1024 + 5]


class TestFileSizes(h.ExofsTestCase):

    def _roundtrip(self, size):
        path = self.path("size-%d" % size)
        data = h.pattern(size, seed=size)
        h.write_file(path, data)
        self.assertEqual(os.stat(path).st_size, size)
        h.assert_same_data(data, h.read_file(path), "size %d (page cache)" % size)
        h.drop_caches()
        h.assert_same_data(data, h.read_file(path), "size %d (after drop_caches)" % size)
        self.remount()
        self.assertEqual(os.stat(path).st_size, size, "size %d not persisted" % size)
        h.assert_same_data(data, h.read_file(path), "size %d (after remount)" % size)


def _make_size_test(size):
    def test(self):
        self._roundtrip(size)
    test.__name__ = "test_roundtrip_%07d_bytes" % size
    test.__doc__ = "write/read %d bytes, verify via cache, drop_caches and remount" % size
    return test


for _size in SIZES:
    _test = _make_size_test(_size)
    setattr(TestFileSizes, _test.__name__, _test)
del _size, _test


class TestLargeFiles(h.ExofsTestCase):

    def test_large_file_streamed(self):
        size = h.LARGE_FILE_MB * 1024 * 1024
        path = self.path("large.bin")
        chunk = 1024 * 1024
        import hashlib
        digest = hashlib.sha256()
        start = time.time()
        handle = open(path, "wb")
        try:
            for i in range(size // chunk):
                block = h.pattern(chunk, seed=i * 100000)
                digest.update(block)
                handle.write(block)
            handle.flush()
            os.fsync(handle.fileno())
        finally:
            handle.close()
        print("wrote %d MB in %.1fs" % (h.LARGE_FILE_MB, time.time() - start))
        self.assertEqual(os.stat(path).st_size, size)
        self.remount()
        start = time.time()
        self.assertEqual(h.sha256_file(path), digest.hexdigest(), "large file corrupted after remount")
        print("read back %d MB in %.1fs" % (h.LARGE_FILE_MB, time.time() - start))

    def test_large_file_random_reads(self):
        size = 2 * 1024 * 1024 + 333
        path = self.path("random-read.bin")
        data = h.pattern(size, seed=7)
        h.write_file(path, data)
        self.remount()
        import random
        rnd = random.Random(1234)
        fd = os.open(path, os.O_RDONLY)
        try:
            for _ in range(200):
                offset = rnd.randint(0, size - 1)
                length = rnd.randint(1, 3 * PAGE)
                os.lseek(fd, offset, os.SEEK_SET)
                got = os.read(fd, length)
                h.assert_same_data(data[offset:offset + length], got,
                                   "pread offset=%d length=%d" % (offset, length))
        finally:
            os.close(fd)

    def test_many_files(self):
        count = h.MANY_FILES
        for i in range(count):
            h.write_file(self.path("f%05d" % i), h.pattern(i % 9000 + 1, seed=i))
        self.assertEqual(len(os.listdir(self.dir)), count)
        self.remount()
        names = sorted(os.listdir(self.dir))
        self.assertEqual(names, ["f%05d" % i for i in range(count)])
        for i in range(count):
            h.assert_same_data(h.pattern(i % 9000 + 1, seed=i), h.read_file(self.path("f%05d" % i)),
                               "file %d of %d" % (i, count))


# ---------------------------------------------------------------------------
# Files: modifications
# ---------------------------------------------------------------------------
class TestFileModify(h.ExofsTestCase):

    def test_overwrite_same_session(self):
        path = self.path("ow")
        h.write_file(path, h.pattern(3 * PAGE, seed=1))
        h.write_file(path, h.pattern(3 * PAGE, seed=2))
        self.remount()
        h.assert_same_data(h.pattern(3 * PAGE, seed=2), h.read_file(path), "overwrite")

    def test_pwrite_middle_same_session(self):
        path = self.path("mid")
        data = bytearray(h.pattern(3 * PAGE, seed=3))
        fd = os.open(path, os.O_CREAT | os.O_RDWR, 0o644)
        try:
            os.write(fd, bytes(data))
            os.lseek(fd, PAGE - 10, os.SEEK_SET)
            os.write(fd, b"Z" * 20)        # crosses a page boundary
            os.fsync(fd)
        finally:
            os.close(fd)
        data[PAGE - 10:PAGE + 10] = b"Z" * 20
        h.assert_same_data(bytes(data), h.read_file(path), "pwrite (cache)")
        self.remount()
        h.assert_same_data(bytes(data), h.read_file(path), "pwrite (remount)")

    def test_append_same_session(self):
        path = self.path("app")
        h.write_file(path, b"abc")
        h.write_file(path, b"def", mode="ab")
        h.write_file(path, h.pattern(PAGE), mode="ab")
        self.remount()
        h.assert_same_data(b"abcdef" + h.pattern(PAGE), h.read_file(path), "append")

    def test_sparse_file(self):
        path = self.path("sparse")
        hole_end = 5 * PAGE + 100
        fd = os.open(path, os.O_CREAT | os.O_WRONLY, 0o644)
        try:
            os.write(fd, b"head")
            os.lseek(fd, hole_end, os.SEEK_SET)
            os.write(fd, b"tail")
            os.fsync(fd)
        finally:
            os.close(fd)
        expected = b"head" + b"\0" * (hole_end - 4) + b"tail"
        self.assertEqual(os.stat(path).st_size, hole_end + 4)
        h.assert_same_data(expected, h.read_file(path), "sparse (cache)")
        self.remount()
        self.assertEqual(os.stat(path).st_size, hole_end + 4)
        h.assert_same_data(expected, h.read_file(path), "sparse (remount)")

    def test_truncate_shrink(self):
        path = self.path("shrink")
        data = h.pattern(5 * PAGE + 50)
        h.write_file(path, data)
        fd = os.open(path, os.O_RDWR)
        try:
            os.ftruncate(fd, PAGE + 10)
        finally:
            os.close(fd)
        self.assertEqual(os.stat(path).st_size, PAGE + 10)
        h.assert_same_data(data[:PAGE + 10], h.read_file(path), "shrink (cache)")
        self.remount()
        self.assertEqual(os.stat(path).st_size, PAGE + 10)
        h.assert_same_data(data[:PAGE + 10], h.read_file(path), "shrink (remount)")

    def test_truncate_to_zero_then_rewrite(self):
        path = self.path("zero")
        h.write_file(path, h.pattern(3 * PAGE, seed=1))
        fd = os.open(path, os.O_RDWR)
        try:
            os.ftruncate(fd, 0)
            os.write(fd, b"new")
            os.fsync(fd)
        finally:
            os.close(fd)
        self.remount()
        self.assertEqual(h.read_file(path), b"new")

    @h.known_issue("exofs_setattr(): truncate does not resize the backing KV object "
                   "(TODO in inode.c) - growing a file reads stale data / fails after remount")
    def test_truncate_extend_reads_zeros(self):
        path = self.path("extend")
        h.write_file(path, h.pattern(2 * PAGE))
        fd = os.open(path, os.O_RDWR)
        try:
            os.ftruncate(fd, 10)          # shrink ...
            os.ftruncate(fd, 3 * PAGE)    # ... and grow again: must read zeros
        finally:
            os.close(fd)
        expected = h.pattern(2 * PAGE)[:10] + b"\0" * (3 * PAGE - 10)
        h.assert_same_data(expected, h.read_file(path), "extend (cache)")
        self.remount()
        self.assertEqual(os.stat(path).st_size, 3 * PAGE)
        h.assert_same_data(expected, h.read_file(path), "extend (remount)")

    @h.known_issue("exofs_write_begin(): 'if (ret)' treats the positive byte count of "
                   "nvme_submit_sync_kv_read() as an error and returns it with folio=NULL "
                   "-> kernel oops on any write into an existing, uncached page "
                   "(fix: 'if (ret < 0)')")
    def test_append_after_remount(self):
        path = self.path("append-remount")
        h.write_file(path, b"first-")
        self.remount()
        h.write_file(path, b"second", mode="ab")
        self.remount()
        self.assertEqual(h.read_file(path), b"first-second")

    @h.known_issue("exofs_write_begin(): positive kv_read result handled as error (see above)")
    def test_modify_middle_after_remount(self):
        path = self.path("modify-remount")
        data = bytearray(h.pattern(3 * PAGE))
        h.write_file(path, bytes(data))
        self.remount()
        fd = os.open(path, os.O_RDWR)
        try:
            os.lseek(fd, PAGE + 5, os.SEEK_SET)
            os.write(fd, b"MODIFIED")
        finally:
            os.close(fd)
        data[PAGE + 5:PAGE + 13] = b"MODIFIED"
        self.remount()
        h.assert_same_data(bytes(data), h.read_file(path), "modify after remount")

    def test_mmap_write(self):
        path = self.path("mmap")
        size = 3 * PAGE
        h.write_file(path, b"\0" * size)
        fd = os.open(path, os.O_RDWR)
        try:
            region = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
            try:
                region[0:5] = b"start"
                region[PAGE * 2 + 100:PAGE * 2 + 103] = b"end"
                region.flush()
            finally:
                region.close()
        finally:
            os.close(fd)
        expected = bytearray(size)
        expected[0:5] = b"start"
        expected[PAGE * 2 + 100:PAGE * 2 + 103] = b"end"
        self.remount()
        h.assert_same_data(bytes(expected), h.read_file(path), "mmap write")

    def test_mmap_read(self):
        path = self.path("mmap-read")
        data = h.pattern(2 * PAGE + 99)
        h.write_file(path, data)
        self.remount()
        fd = os.open(path, os.O_RDONLY)
        try:
            region = mmap.mmap(fd, len(data), mmap.MAP_SHARED, mmap.PROT_READ)
            try:
                h.assert_same_data(data, region[:], "mmap read")
            finally:
                region.close()
        finally:
            os.close(fd)

    def test_concurrent_writers(self):
        errors = []
        size = 128 * 1024 + 17

        def worker(index):
            try:
                h.write_file(self.path("t%d" % index), h.pattern(size, seed=index))
            except Exception as error:      # pylint: disable=broad-except
                errors.append("thread %d: %s" % (index, error))

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(6)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        self.assertEqual(errors, [])
        self.remount()
        for i in range(6):
            h.assert_same_data(h.pattern(size, seed=i), h.read_file(self.path("t%d" % i)),
                               "concurrent file %d" % i)


# ---------------------------------------------------------------------------
# Directories
# ---------------------------------------------------------------------------
class TestDirectories(h.ExofsTestCase):

    def test_mkdir_rmdir(self):
        path = self.path("d")
        parent_links = os.stat(self.dir).st_nlink
        os.mkdir(path)
        st = os.stat(path)
        self.assertTrue(stat.S_ISDIR(st.st_mode))
        self.assertEqual(os.stat(self.dir).st_nlink, parent_links + 1, "parent nlink not incremented")
        self.assertEqual(os.listdir(path), [])
        self.remount()
        self.assertTrue(os.path.isdir(path))
        self.assertEqual(os.stat(self.dir).st_nlink, parent_links + 1)
        os.rmdir(path)
        self.assertFalse(os.path.lexists(path))
        self.assertEqual(os.stat(self.dir).st_nlink, parent_links)
        self.remount()
        self.assertFalse(os.path.lexists(path))

    @h.known_issue("exofs_mkdir() never increments the new directory's own link count "
                   "(ext2 does inode_inc_link_count), so an empty directory has nlink 1 instead of 2")
    def test_directory_link_counts(self):
        os.mkdir(self.path("d"))
        self.assertEqual(os.stat(self.path("d")).st_nlink, 2)
        os.mkdir(self.path("d", "sub"))
        self.assertEqual(os.stat(self.path("d")).st_nlink, 3)
        self.remount()
        self.assertEqual(os.stat(self.path("d")).st_nlink, 3)
        self.assertEqual(os.stat(self.path("d", "sub")).st_nlink, 2)

    def test_dot_and_dotdot(self):
        sub = self.path("sub")
        os.mkdir(sub)
        self.remount()
        self.assertEqual(os.stat(os.path.join(sub, ".")).st_ino, os.stat(sub).st_ino)
        self.assertEqual(os.stat(os.path.join(sub, "..")).st_ino, os.stat(self.dir).st_ino)

    def test_mkdir_errors(self):
        os.mkdir(self.path("exists"))
        h.write_file(self.path("file"), b"x")
        h.assert_errno(errno.EEXIST, os.mkdir, self.path("exists"))
        h.assert_errno(errno.EEXIST, os.mkdir, self.path("file"))
        h.assert_errno(errno.ENOENT, os.mkdir, self.path("missing", "child"))
        h.assert_errno(errno.ENOTDIR, os.mkdir, self.path("file", "child"))

    def test_rmdir_errors(self):
        os.mkdir(self.path("full"))
        h.write_file(self.path("full", "inside"), b"x")
        h.write_file(self.path("file"), b"x")
        h.assert_errno((errno.ENOTEMPTY, errno.EEXIST), os.rmdir, self.path("full"))
        h.assert_errno(errno.ENOENT, os.rmdir, self.path("missing"))
        h.assert_errno(errno.ENOTDIR, os.rmdir, self.path("file"))
        h.assert_errno(errno.EISDIR, os.unlink, self.path("full"))
        self.assertTrue(os.path.isfile(self.path("full", "inside")))

    def test_nested_directories(self):
        depth = 25
        parts = ["level%02d" % i for i in range(depth)]
        deepest = self.path(*parts)
        os.makedirs(deepest)
        h.write_file(os.path.join(deepest, "leaf"), b"deep data")
        self.remount()
        self.assertEqual(h.read_file(os.path.join(deepest, "leaf")), b"deep data")
        current = self.dir
        for part in parts:
            self.assertEqual(os.listdir(current), [part])
            current = os.path.join(current, part)

    def test_directory_spanning_many_pages(self):
        # ~80 byte records -> several 4K directory pages
        names = ["entry-%04d-%s" % (i, "x" * 60) for i in range(300)]
        for name in names:
            open(self.path(name), "wb").close()
        self.assertEqual(sorted(os.listdir(self.dir)), sorted(names))
        self.remount()
        self.assertEqual(sorted(os.listdir(self.dir)), sorted(names))
        # delete every second entry, re-list, then re-use the freed space
        for name in names[::2]:
            os.unlink(self.path(name))
        self.assertEqual(sorted(os.listdir(self.dir)), sorted(names[1::2]))
        self.remount()
        self.assertEqual(sorted(os.listdir(self.dir)), sorted(names[1::2]))
        new_names = ["reuse-%04d" % i for i in range(100)]
        for name in new_names:
            open(self.path(name), "wb").close()
        self.remount()
        self.assertEqual(sorted(os.listdir(self.dir)), sorted(names[1::2] + new_names))
        for name in names[1::2] + new_names:
            self.assertTrue(os.path.isfile(self.path(name)), "lost %s" % name)

    def test_readdir_has_no_duplicates(self):
        for i in range(50):
            os.mkdir(self.path("d%02d" % i))
            open(self.path("f%02d" % i), "wb").close()
        listing = os.listdir(self.dir)
        self.assertEqual(len(listing), len(set(listing)))
        self.assertEqual(len(listing), 100)

    def test_remove_tree(self):
        for i in range(5):
            sub = self.path("d%d" % i, "inner")
            os.makedirs(sub)
            for j in range(5):
                h.write_file(os.path.join(sub, "f%d" % j), h.pattern(100 * j + 1))
        before = h.statvfs_files()
        import shutil
        shutil.rmtree(self.path("d0"))
        self.assertFalse(os.path.lexists(self.path("d0")))
        self.assertTrue(h.wait_for(lambda: h.statvfs_files() == before - 7),
                        "f_files %d, expected %d" % (h.statvfs_files(), before - 7))
        self.remount()
        self.assertEqual(sorted(os.listdir(self.dir)), ["d1", "d2", "d3", "d4"])


# ---------------------------------------------------------------------------
# Names
# ---------------------------------------------------------------------------
class TestNames(h.ExofsTestCase):

    def test_max_name_length(self):
        name = "n" * h.EXOFS_NAME_LEN
        h.write_file(self.path(name), b"long name")
        self.remount()
        self.assertEqual(os.listdir(self.dir), [name])
        self.assertEqual(h.read_file(self.path(name)), b"long name")

    def test_name_too_long(self):
        name = "n" * (h.EXOFS_NAME_LEN + 1)
        h.assert_errno(errno.ENAMETOOLONG, os.open, self.path(name), os.O_CREAT | os.O_WRONLY, 0o644)
        h.assert_errno(errno.ENAMETOOLONG, os.mkdir, self.path(name))

    def test_special_characters(self):
        names = ["with space", "dash-under_score.dot", "UPPER lower", "#hash%percent",
                 "tab\there", "new\nline", "quote'\"", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d",
                 ".hidden", "..double", "a" * 1]
        for index, name in enumerate(names):
            h.write_file(self.path(name), ("%d" % index).encode("ascii"))
        self.remount()
        listing = os.listdir(self.dir)
        self.assertEqual(sorted(listing), sorted(names))
        for index, name in enumerate(names):
            self.assertEqual(h.read_file(self.path(name)), ("%d" % index).encode("ascii"))

    def test_names_are_case_sensitive(self):
        h.write_file(self.path("Case"), b"1")
        h.write_file(self.path("case"), b"2")
        self.remount()
        self.assertEqual(h.read_file(self.path("Case")), b"1")
        self.assertEqual(h.read_file(self.path("case")), b"2")

    def test_prefix_names_are_distinct(self):
        # lookup must compare the full name, not a prefix
        for name in ["ab", "abc", "abcd", "a"]:
            h.write_file(self.path(name), name.encode("ascii"))
        self.remount()
        for name in ["ab", "abc", "abcd", "a"]:
            self.assertEqual(h.read_file(self.path(name)), name.encode("ascii"))
        h.assert_errno(errno.ENOENT, os.stat, self.path("abcde"))


# ---------------------------------------------------------------------------
# Rename
# ---------------------------------------------------------------------------
class TestRename(h.ExofsTestCase):

    def test_rename_same_dir(self):
        h.write_file(self.path("old"), b"payload")
        ino = os.stat(self.path("old")).st_ino
        os.rename(self.path("old"), self.path("new"))
        self.assertFalse(os.path.lexists(self.path("old")))
        self.assertEqual(os.stat(self.path("new")).st_ino, ino)
        self.remount()
        self.assertEqual(os.listdir(self.dir), ["new"])
        self.assertEqual(h.read_file(self.path("new")), b"payload")

    def test_rename_across_dirs(self):
        os.mkdir(self.path("a"))
        os.mkdir(self.path("b"))
        h.write_file(self.path("a", "f"), h.pattern(PAGE + 5))
        os.rename(self.path("a", "f"), self.path("b", "g"))
        self.remount()
        self.assertEqual(os.listdir(self.path("a")), [])
        self.assertEqual(os.listdir(self.path("b")), ["g"])
        h.assert_same_data(h.pattern(PAGE + 5), h.read_file(self.path("b", "g")), "moved file")

    @h.known_issue("exofs_rename(): exofs_set_link() requires a locked folio but the one "
                   "from exofs_find_entry() is unlocked, so replacing an existing name "
                   "always fails with EINVAL; fix: folio_lock()/folio_unlock() around "
                   "exofs_set_link() in exofs_rename() (as exofs_delete_entry() does)")
    def test_rename_replaces_existing_file(self):
        h.write_file(self.path("src"), b"source")
        h.write_file(self.path("dst"), b"destination")
        before = h.statvfs_files()
        os.rename(self.path("src"), self.path("dst"))
        self.assertEqual(h.read_file(self.path("dst")), b"source")
        self.assertTrue(h.wait_for(lambda: h.statvfs_files() == before - 1),
                        "replaced inode not released (f_files %d, expected %d)"
                        % (h.statvfs_files(), before - 1))
        self.remount()
        self.assertEqual(os.listdir(self.dir), ["dst"])
        self.assertEqual(h.read_file(self.path("dst")), b"source")

    def test_rename_to_itself(self):
        h.write_file(self.path("same"), b"same")
        os.rename(self.path("same"), self.path("same"))
        self.assertEqual(h.read_file(self.path("same")), b"same")

    def test_rename_errors(self):
        os.mkdir(self.path("dir"))
        h.write_file(self.path("file"), b"x")
        h.assert_errno(errno.ENOENT, os.rename, self.path("missing"), self.path("x"))
        h.assert_errno(errno.EISDIR, os.rename, self.path("file"), self.path("dir"))
        h.assert_errno(errno.ENOTDIR, os.rename, self.path("dir"), self.path("file"))
        h.assert_errno(errno.EINVAL, os.rename, self.path("dir"), self.path("dir", "inside"))

    def test_rename_directory_same_parent(self):
        os.makedirs(self.path("d1", "child"))
        h.write_file(self.path("d1", "child", "f"), b"in dir")
        os.rename(self.path("d1"), self.path("d2"))
        self.remount()
        self.assertEqual(os.listdir(self.dir), ["d2"])
        self.assertEqual(h.read_file(self.path("d2", "child", "f")), b"in dir")

    @h.known_issue("exofs_rename(): moving a directory to a new parent does not update its "
                   "'..' entry nor the nlink of the old/new parent")
    def test_rename_directory_across_parents(self):
        os.mkdir(self.path("p1"))
        os.mkdir(self.path("p2"))
        os.mkdir(self.path("p1", "moved"))
        p1_links = os.stat(self.path("p1")).st_nlink
        p2_links = os.stat(self.path("p2")).st_nlink
        os.rename(self.path("p1", "moved"), self.path("p2", "moved"))
        self.remount()
        self.assertEqual(os.stat(self.path("p2", "moved", "..")).st_ino, os.stat(self.path("p2")).st_ino)
        self.assertEqual(os.stat(self.path("p1")).st_nlink, p1_links - 1)
        self.assertEqual(os.stat(self.path("p2")).st_nlink, p2_links + 1)

    @h.known_issue("exofs_rename(): replacing a non-empty directory is not rejected (must be ENOTEMPTY)")
    def test_rename_over_non_empty_directory(self):
        os.mkdir(self.path("src"))
        os.mkdir(self.path("dst"))
        h.write_file(self.path("dst", "keep"), b"keep")
        h.assert_errno((errno.ENOTEMPTY, errno.EEXIST), os.rename, self.path("src"), self.path("dst"))
        self.assertEqual(h.read_file(self.path("dst", "keep")), b"keep")


# ---------------------------------------------------------------------------
# Hard links and symlinks
# ---------------------------------------------------------------------------
class TestLinks(h.ExofsTestCase):

    def test_hard_link(self):
        h.write_file(self.path("orig"), b"shared")
        os.link(self.path("orig"), self.path("link"))
        st1, st2 = os.stat(self.path("orig")), os.stat(self.path("link"))
        self.assertEqual(st1.st_ino, st2.st_ino)
        self.assertEqual(st1.st_nlink, 2)
        h.write_file(self.path("link"), b"changed via link")
        self.assertEqual(h.read_file(self.path("orig")), b"changed via link")
        self.remount()
        self.assertEqual(os.stat(self.path("orig")).st_nlink, 2)
        os.unlink(self.path("orig"))
        self.assertEqual(os.stat(self.path("link")).st_nlink, 1)
        self.remount()
        self.assertEqual(h.read_file(self.path("link")), b"changed via link")
        self.assertEqual(os.stat(self.path("link")).st_nlink, 1)

    def test_hard_link_errors(self):
        os.mkdir(self.path("dir"))
        h.write_file(self.path("file"), b"x")
        h.assert_errno(errno.EPERM, os.link, self.path("dir"), self.path("dirlink"))
        h.assert_errno(errno.EEXIST, os.link, self.path("file"), self.path("file"))
        h.assert_errno(errno.ENOENT, os.link, self.path("missing"), self.path("l"))

    def _symlink_roundtrip(self, target):
        link = self.path("link")
        os.symlink(target, link)
        self.assertTrue(stat.S_ISLNK(os.lstat(link).st_mode))
        self.assertEqual(os.readlink(link), target)
        self.assertEqual(os.lstat(link).st_size, len(target))
        self.remount()
        got = os.readlink(link)
        self.assertEqual(got, target, "symlink of %d bytes lost on remount (read back %r)" % (len(target), got[:40]))
        self.assertEqual(os.lstat(link).st_size, len(target))

    def test_fast_symlink(self):
        self._symlink_roundtrip("abc")

    def test_fast_symlink_max_inline(self):
        self._symlink_roundtrip("f" * h.EXOFS_FAST_SYMLINK_MAX)

    @h.known_issue("exofs_new_inode() never clears ei->i_data (slab memory is reused), "
                   "so a slow symlink persists stale bytes and is read back as a fast "
                   "symlink after remount; fix: memset(ei->i_data, 0, sizeof(ei->i_data))")
    def test_slow_symlink_just_over_inline(self):
        self._symlink_roundtrip("s" * (h.EXOFS_FAST_SYMLINK_MAX + 1))

    @h.known_issue("exofs_new_inode() never clears ei->i_data (slab memory is reused), "
                   "so a slow symlink persists stale bytes and is read back as a fast "
                   "symlink after remount; fix: memset(ei->i_data, 0, sizeof(ei->i_data))")
    def test_slow_symlink_long(self):
        self._symlink_roundtrip("/".join(["component%03d" % i for i in range(90)]))   # ~1.2K

    def test_symlink_follow(self):
        h.write_file(self.path("target"), b"via symlink")
        os.symlink("target", self.path("rel"))
        self.remount()
        self.assertEqual(h.read_file(self.path("rel")), b"via symlink")

    @h.known_issue("exofs_new_inode() never clears ei->i_data (slab memory is reused), "
                   "so a slow symlink persists stale bytes and is read back as a fast "
                   "symlink after remount; fix: memset(ei->i_data, 0, sizeof(ei->i_data))")
    def test_symlink_follow_absolute(self):
        # the absolute path is longer than the inline area -> slow symlink
        h.write_file(self.path("target"), b"via symlink")
        os.symlink(self.path("target"), self.path("abs"))
        self.remount()
        self.assertEqual(h.read_file(self.path("abs")), b"via symlink")

    def test_dangling_symlink(self):
        os.symlink("nowhere", self.path("dangling"))
        self.assertEqual(os.readlink(self.path("dangling")), "nowhere")
        h.assert_errno(errno.ENOENT, os.stat, self.path("dangling"))

    def test_symlink_loop(self):
        os.symlink("loop2", self.path("loop1"))
        os.symlink("loop1", self.path("loop2"))
        h.assert_errno(errno.ELOOP, os.stat, self.path("loop1"))

    def test_unlink_symlink_keeps_target(self):
        h.write_file(self.path("target"), b"stay")
        os.symlink("target", self.path("link"))
        os.unlink(self.path("link"))
        self.remount()
        self.assertEqual(os.listdir(self.dir), ["target"])
        self.assertEqual(h.read_file(self.path("target")), b"stay")


# ---------------------------------------------------------------------------
# Special files (mknod)
# ---------------------------------------------------------------------------
class TestSpecialFiles(h.ExofsTestCase):

    def test_fifo(self):
        os.mkfifo(self.path("fifo"), 0o600)
        self.remount()
        self.assertTrue(stat.S_ISFIFO(os.lstat(self.path("fifo")).st_mode))

    def test_fifo_carries_data(self):
        path = self.path("pipe")
        os.mkfifo(path, 0o600)
        received = []

        def reader():
            handle = open(path, "rb")
            try:
                received.append(handle.read())
            finally:
                handle.close()

        thread = threading.Thread(target=reader)
        thread.start()
        handle = open(path, "wb")
        try:
            handle.write(b"through the fifo")
        finally:
            handle.close()
        thread.join(10)
        self.assertEqual(received, [b"through the fifo"])

    def test_char_device_node(self):
        os.mknod(self.path("null"), stat.S_IFCHR | 0o666, os.makedev(1, 3))
        self.remount()
        st = os.lstat(self.path("null"))
        self.assertTrue(stat.S_ISCHR(st.st_mode))
        self.assertEqual((os.major(st.st_rdev), os.minor(st.st_rdev)), (1, 3))
        handle = open(self.path("null"), "wb")           # really behaves like /dev/null
        try:
            handle.write(b"discarded")
        finally:
            handle.close()
        self.assertEqual(h.read_file(self.path("null")), b"")

    def test_block_device_node_large_numbers(self):
        # major/minor that need the "new" 32 bit encoding
        os.mknod(self.path("blk"), stat.S_IFBLK | 0o600, os.makedev(259, 70000))
        self.remount()
        st = os.lstat(self.path("blk"))
        self.assertTrue(stat.S_ISBLK(st.st_mode))
        self.assertEqual((os.major(st.st_rdev), os.minor(st.st_rdev)), (259, 70000))

    def test_regular_mknod(self):
        os.mknod(self.path("regular"), stat.S_IFREG | 0o644)
        self.assertTrue(stat.S_ISREG(os.stat(self.path("regular")).st_mode))

    @h.known_issue("exofs_tmpfile() never calls finish_open_simple(), "
                   "so O_TMPFILE fails (EINVAL + WARN) on kernels >= 6.3")
    def test_o_tmpfile(self):
        o_tmpfile = getattr(os, "O_TMPFILE", 0o20000000 | os.O_DIRECTORY)
        fd = os.open(self.dir, o_tmpfile | os.O_RDWR, 0o600)
        try:
            os.write(fd, b"anonymous")
            self.assertEqual(os.listdir(self.dir), [])
        finally:
            os.close(fd)


# ---------------------------------------------------------------------------
# Metadata
# ---------------------------------------------------------------------------
class TestMetadata(h.ExofsTestCase):

    def test_chmod_persists(self):
        path = self.path("perm")
        h.write_file(path, b"x")
        for mode in (0o600, 0o755, 0o4711, 0o000):
            os.chmod(path, mode)
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), mode)
        os.chmod(path, 0o751)
        self.remount()
        self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o751)

    def test_chmod_directory_persists(self):
        os.mkdir(self.path("d"))
        os.chmod(self.path("d"), 0o711)
        self.remount()
        self.assertEqual(stat.S_IMODE(os.stat(self.path("d")).st_mode), 0o711)

    def test_chown_persists(self):
        path = self.path("owner")
        h.write_file(path, b"x")
        os.chown(path, 1234, 4321)
        self.remount()
        st = os.stat(path)
        self.assertEqual((st.st_uid, st.st_gid), (1234, 4321))

    def test_utime_persists(self):
        path = self.path("times")
        h.write_file(path, b"x")
        os.utime(path, (1000000000, 1234567890))
        self.remount()
        st = os.stat(path)
        self.assertEqual(int(st.st_atime), 1000000000)
        self.assertEqual(int(st.st_mtime), 1234567890)

    def test_mtime_updates_on_write(self):
        path = self.path("mtime")
        h.write_file(path, b"x")
        os.utime(path, (1000, 1000))
        h.write_file(path, b"more", mode="ab")
        self.assertGreater(os.stat(path).st_mtime, 1000)
        self.remount()
        self.assertGreater(os.stat(path).st_mtime, 1000, "mtime update not persisted")

    def test_directory_mtime_updates_on_create(self):
        os.utime(self.dir, (1000, 1000))
        open(self.path("new"), "wb").close()
        self.assertGreater(os.stat(self.dir).st_mtime, 1000)

    def test_ctime_updates_on_chmod(self):
        path = self.path("ctime")
        h.write_file(path, b"x")
        before = os.stat(path).st_ctime
        time.sleep(1.1)
        os.chmod(path, 0o600)
        self.assertGreater(os.stat(path).st_ctime, before)

    def test_inode_numbers_unique_and_stable(self):
        inos = {}
        for i in range(20):
            h.write_file(self.path("i%d" % i), b"x")
            inos["i%d" % i] = os.stat(self.path("i%d" % i)).st_ino
        self.assertEqual(len(set(inos.values())), 20, "duplicate inode numbers")
        self.remount()
        for name, ino in inos.items():
            self.assertEqual(os.stat(self.path(name)).st_ino, ino, "inode of %s changed" % name)


# ---------------------------------------------------------------------------
# Persistence of a whole tree
# ---------------------------------------------------------------------------
class TestPersistence(h.ExofsTestCase):

    def _build_tree(self):
        os.makedirs(self.path("docs", "drafts"))
        os.makedirs(self.path("bin"))
        os.makedirs(self.path("empty"))
        h.write_file(self.path("docs", "a.txt"), h.pattern(1000, seed=1))
        h.write_file(self.path("docs", "drafts", "b.txt"), h.pattern(3 * PAGE + 1, seed=2))
        h.write_file(self.path("bin", "tool"), h.pattern(70000, seed=3))
        os.chmod(self.path("bin", "tool"), 0o755)
        os.symlink("../docs/a.txt", self.path("bin", "a-link"))
        if h.STRICT:
            # slow symlink: known exofs issue (stale ei->i_data), see TestLinks
            os.symlink("x" * 300, self.path("bin", "long-dangling"))
        os.link(self.path("docs", "a.txt"), self.path("docs", "a-hard"))
        os.mkfifo(self.path("bin", "fifo"))

    def _manifest(self):
        manifest = {}
        for root, dirs, files in os.walk(self.dir):
            for name in dirs + files:
                path = os.path.join(root, name)
                rel = os.path.relpath(path, self.dir)
                st = os.lstat(path)
                entry = (stat.S_IFMT(st.st_mode), stat.S_IMODE(st.st_mode), st.st_nlink)
                if stat.S_ISREG(st.st_mode):
                    entry += (st.st_size, h.sha256_file(path))
                elif stat.S_ISLNK(st.st_mode):
                    entry += (os.readlink(path),)
                manifest[rel] = entry
        return manifest

    def test_tree_survives_remounts(self):
        self._build_tree()
        expected = self._manifest()
        for _ in range(3):
            self.remount()
            self.assertEqual(self._manifest(), expected)

    def test_tree_survives_drop_caches(self):
        self._build_tree()
        expected = self._manifest()
        h.drop_caches()
        self.assertEqual(self._manifest(), expected)

    def test_unmount_without_explicit_sync(self):
        # data written without fsync must still be flushed by umount
        handle = open(self.path("nosync"), "wb")
        try:
            handle.write(h.pattern(10 * PAGE + 3))
        finally:
            handle.close()
        h.umount()
        h.mount()
        h.assert_same_data(h.pattern(10 * PAGE + 3), h.read_file(self.path("nosync")), "umount flush")
