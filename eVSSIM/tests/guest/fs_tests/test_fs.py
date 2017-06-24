import pytest
import os
from subprocess import call
import glob

NVME_DEVICE='/dev/nvme0n1'
MOUNTPOINT='/mnt/fstest'
BACKUP_DIR='/home/esd'
TEST_NUMBER_OF_ITERATIONS=10

class TestFileSystem(object):
    def mkfs(self):
        assert 0==call(['mkfs.ext4',NVME_DEVICE])

    def mount(self):
        if not os.path.exists(MOUNTPOINT):
           assert 0==call(['mkdir',MOUNTPOINT])
        assert 0==call(['mount', NVME_DEVICE, MOUNTPOINT])

    def generate_files(self):
        for size in [2, 4, 8, 16, 32 ]:
            mb="count={}".format(size)
            targetfile="of={}/target-file_{}".format(MOUNTPOINT,size)
            assert 0==call(['dd','if=/dev/urandom',targetfile,'bs=1M',mb])

    def backup_files(self):
        assert 0==call("cp {}/target-file_* {}".format(MOUNTPOINT,BACKUP_DIR), shell=True )

    def diff_files(self):
        for backupfile in glob.glob(BACKUP_DIR+"/target-file_*"):
            targetfile=MOUNTPOINT+"/"+os.path.basename(backupfile)
            assert 0==call(['diff', backupfile, targetfile])

    def umount(self):
        assert 0==call(['umount',MOUNTPOINT])

    def test_setup(self):
        self.mkfs()
        self.mount()
        self.generate_files()
        self.backup_files()
        self.diff_files()

    def test_iterations(self):
        for _ in range(TEST_NUMBER_OF_ITERATIONS):
          self.umount()
          self.mount()
          self.diff_files()


    def test_cleanup(self):
        assert 0==call("rm {}/target-file_*".format(MOUNTPOINT), shell=True )
        assert 0==call("rm {}/target-file_*".format(BACKUP_DIR), shell=True )
        self.umount()


