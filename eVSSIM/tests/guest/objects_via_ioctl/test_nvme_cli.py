import os
import random
import tempfile
import subprocess
import contextlib
import collections
import unittest


BLOCK_SIZE = 512

def blocks(size):
    assert size
    # Rounding down since qemu automatically adds one block
    return (size - 1) / BLOCK_SIZE


@contextlib.contextmanager
def data(size):
    """
    Provids a context manager that returns a path to a file containing
    random data. The file's lifetime is limited to the scope of the
    context manager.

    :param size: Size of the data stored in the file.
    """
    with tempfile.NamedTemporaryFile() as result:
        contents = os.urandom(size)
        result.write(contents)
        result.flush()
        yield result.name


class NvmeDevice(object):


    def __init__(self, device, nvme_cli_dir):
        self.device = device
        self.nvme_cli_dir = nvme_cli_dir
        
        
    def objw(self, obj_id, data):
        """
        Write the given data to this device in the object described by
        the given object id.

        :param obj_id: String containing object id of the written object.
        :param data: Path to file containing data to write to the specified object.
        """
        size = os.path.getsize(data)
        subprocess.check_call([
            './nvme', 'objw', self.device,
             obj_id,
            '--data=' + data,
        ], cwd=self.nvme_cli_dir)
        
        
    def objr(self, obj_id):
        """
        Read contents of objects described by object id.

        :param obj_id: String containing object id of the read object.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
                './nvme', 'objr', self.device,
                obj_id,
                '--data=' + dest.name,
            ], cwd=self.nvme_cli_dir)
            return dest.read()


    def objl(self):
        """
        Get a list of the objects stored in the device.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
            './nvme', 'objl', self.device, 
            '--data=' + dest.name,
        ], cwd=self.nvme_cli_dir)
            return dest.read()


    def objd(self, obj_id):
        """
        Delete object matching given object id
        
        :param obj_id: String containing object id of the object to be deleted.
        """
        subprocess.check_call([
            './nvme', 'objd', self.device, 
            obj_id,
        ], cwd=self.nvme_cli_dir)



DEVICE_NAME = '/dev/nvme0n1'
NVME_CLI_DIR = '/home/esd/guest'

class test_NvmeCli(object):
    """
    Class containing tests for nvme cli using objects.
    """
    device = NvmeDevice(DEVICE_NAME, NVME_CLI_DIR)
    
        
    def test_list(self):
    	"""
        Test writing objects of random size and checking that the resulting list
        matches the written objects.
        """
        count = 10
        size = 1024*1024
        objects = []
        obj_names = []
        for oid in xrange(1, count + 1):
            with data(size) as input:
                self.device.objw(input, input) # write into object by the same name as the created temp file
                objects.append([input, size])
                obj_names.append(input)
        content = self.device.objl()
        lines = content.split('\n')
        assert len(lines) - 1 == count
        for oid in xrange(0, count):
        	line = lines[oid].split(' ')
        	assert [line[0], int(line[1])] in objects
        print("objects_via_ioctl: test_list finished successfully")
        self.cleanup(obj_names)
    	
    	
    def test_delete(self):
        size = 1024*1024
        with data(size) as input:
            self.device.objw(input, input)
            self.device.objr(input)
            self.device.objd(input)
            try:
            	self.device.objr(input)
            except:
                print("objects_via_ioctl: test_delete finished successfully")
            	return
            else:
                raise Exception("No exception when trying to read deleted object")
            
    
    def test_read(self):
        """
        Test writing and reading objects containing random data
        """
        count = 10
        size = 1024*1024
        obj_names = []
        for oid in xrange(1, count + 1):
            with data(size) as input:
                self.device.objw(input, input) # write into object by the same name as the created temp file
                obj_names.append(input)
                with open(input, 'rb') as src:
                    assert src.read() == self.device.objr(input)
        print("objects_via_ioctl: test_read finished successfully")
        self.cleanup(obj_names)
    
    
    def test_write(self):
        """
        This test currently does nothing
        """
        return
    
    
    def cleanup(self, objects):
    	for obj in objects:
    		self.device.objd(obj)

