import subprocess
import json
import os
from tempfile import NamedTemporaryFile

NVME_CLI='/usr/local/sbin/nvme'

def nvme(*args):
   return subprocess.run(
      [NVME_CLI, *args],
      check=True,
      capture_output=True
   )

def namespaces() -> list[dict]:
   output = nvme('list', '-o', 'json').stdout
   return json.loads(output).get('Devices', [])

class TestNvmeDisk:
   def test_at_least_one_nvme_disk_exists(self):
      assert namespaces(), "No NVMe namespaces found!"

   def test_controllers_and_namespaces_respond(self):
      ns_list = namespaces()
      for ns in ns_list:
         ns_path = ns["DevicePath"]

         # Check that the namespace and controllers respond without the command failing
         nvme('id-ns', ns_path)
         nvme('id-ctrl', ns_path)

   def test_read(self):
      ns_list = namespaces()
      for ns in ns_list:
         ns_path = ns["DevicePath"]
         sector_size = ns["SectorSize"]

         # Test that a read command can be issued successfully
         nvme('read', ns_path,
              '--start-block', '0',
              '--block-count', '0',
              '--data-size', str(sector_size))

   def test_write(self):
      ns_list = namespaces()
      with NamedTemporaryFile() as tmp_write, NamedTemporaryFile() as tmp_read:
         for ns in ns_list:
            ns_path = ns["DevicePath"]
            sector_size = ns["SectorSize"]

            # Write `sector_size` random bytes to the start of the file
            tmp_write.seek(0)
            tmp_write.write(os.urandom(sector_size))
            tmp_write.flush()

            # Write the file to the current nvme namespace
            nvme('write', ns_path,
                 '--start-block', '0',
                 '--block-count', '0',
                 '--data-size', str(sector_size),
                 '--data', tmp_write.name)

            # Read the sector that was written and compare the data that was
            # read to the data that was supposed to be written
            nvme('read', ns_path,
                 '--start-block', '0',
                 '--block-count', '0',
                 '--data-size', str(sector_size),
                 '--data', tmp_read.name)
            tmp_read.seek(0)
            tmp_write.seek(0)
            assert tmp_read.read() == tmp_write.read(), (
               f"Write Test Failed: Writing data to '{ns_path}' and reading it back resulted in different data!"
            )
