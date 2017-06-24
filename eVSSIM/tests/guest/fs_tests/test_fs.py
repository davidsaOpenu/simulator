import pytest
import os
from subprocess import call
import glob

NVME_DEVICE='/dev/nvme0n1'
MOUNTPOINT='/mnt/fstest'
BACKUP_DIR='/home/esd'
COMMON_DIR='/home/esd/guest/common'
TEST_NUMBER_OF_ITERATIONS=10


def rmmod():
    assert 0==call(['rmmod','nvme'])

def insmod():
    assert 0==call(['modprobe','nvme'])

def verify_dmesg(clear=False):
    if clear:
       return 1==call(COMMON_DIR+"/check-dmesg.sh -C")
    return 1==call(COMMON_DIR+"/check-dmesg.sh -l=warn,err,-s='ext4\|nvme'")

def sync_and_verify():
    assert 0==call(['free'])
    assert verify_dmesg()
    assert 0==call(['sync'])
    assert verify_dmesg()
    assert 0==call("echo 3> /proc/sys/vm/drop_caches", shell=True)
    assert verify_dmesg()
    assert 0==call(['free'])
    assert verify_dmesg()

def mkfs():
    assert 0==call(['mkfs.ext4',NVME_DEVICE])

def mount():
    if not os.path.exists(MOUNTPOINT):
       assert 0==call(['mkdir',MOUNTPOINT])
    assert 0==call(['mount', NVME_DEVICE, MOUNTPOINT])

def generate_files():
    for size in [2, 4, 8, 16, 32 ]:
        mb="count={}".format(size)
        targetfile="of={}/target-file_{}".format(MOUNTPOINT,size)
        assert 0==call(['dd','if=/dev/urandom',targetfile,'bs=1M',mb])

def backup_files():
    assert 0==call("cp {}/target-file_* {}".format(MOUNTPOINT,BACKUP_DIR), shell=True )

def diff_files():
    for backupfile in glob.glob(BACKUP_DIR+"/target-file_*"):
        targetfile=MOUNTPOINT+"/"+os.path.basename(backupfile)
        assert 0==call(['diff', backupfile, targetfile])

def umount():
    assert 0==call(['umount',MOUNTPOINT])

class TestFileSystem(object):

    def test_setup(self):
        call(['rmmod','nvme'])
        verify_dmesg(clear=True)
        insmod()
        mkfs()
        sync_and_verify()
        mount()
        sync_and_verify()
        generate_files()
        sync_and_verify()
        backup_files()
        sync_and_verify()
        diff_files()

    def test_iterations(self):
        for _ in range(TEST_NUMBER_OF_ITERATIONS):
          umount()
          rmmod()
          insmod()
          sync_and_verify()
          mount()
          diff_files()


    def test_cleanup(self):
        assert 0==call("rm {}/target-file_*".format(MOUNTPOINT), shell=True )
        assert 0==call("rm {}/target-file_*".format(BACKUP_DIR), shell=True )
        umount()


