import subprocess
from glob import glob

NVME_DISK='/dev/nvme0n1'

class TestNvmeDisk:
   def test_at_least_one_nvme_disk_exists(self):
      assert len(glob('/dev/nvme*')) > 0, "No NVMe devices found!"

   def test_read(self):
      assert 0 == subprocess.run(['dd', f'if={NVME_DISK}', 'of=/dev/null', 'bs=4k', 'count=1']).returncode, (
         f"Read Test Failed: Failed to read from '{NVME_DISK}'!"
      )

   def test_write(self):
      tmpFileIn = subprocess.check_output(['mktemp'], text=True)
      tmpFileOut = subprocess.check_output(['mktemp'], text=True)
      subprocess.run(['dd', 'if=/dev/urandom', f'of={tmpFileIn}', 'bs=4k', 'count=1'], check=True)
      subprocess.run(['dd', f'if={tmpFileIn}', f'of={NVME_DISK}', 'bs=4k', 'count=1'], check=True)
      subprocess.run(['dd', f'if={NVME_DISK}', f'of={tmpFileOut}', 'bs=4k', 'count=1'], check=True)
      assert 0 == subprocess.run(['cmp', tmpFileIn, tmpFileOut]).returncode, (
         f"Write Test Failed: Writing data to '{NVME_DISK}' and reading it back resulted in different data!"
      )
