import os
import math
import random
import tempfile
import subprocess
import contextlib
import collections
import unittest


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
        
        
    def objw(self, obj_id):
        """
        Write the given data to this device in the object described by
        the given object id.

        :param obj_id: String containing object id of the written object.
        """
        subprocess.check_call([
            './nvme', 'objw', self.device,
             obj_id,
        ], cwd=self.nvme_cli_dir)
        
        
    def objr(self, obj_id):
        """
        Read contents of objects described by object id.

        :param obj_id: String containing object id of the read object.
        """
        with open(obj_id, "rb") as dest:
            subprocess.check_call([
                './nvme', 'objr', self.device,
                obj_id,
            ], cwd=self.nvme_cli_dir)
            return dest.read()


    def objl(self):
        """
        Get a list of the objects stored in the device.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
            './nvme', 'objl', self.device, 
        ], cwd=self.nvme_cli_dir, stdout=dest)
            dest.seek(0)
            return dest.read()


    def objd(self, obj_id):
        """
        Delete object matching given object id
        
        :param obj_id: ID of object that will be deleted
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
    
    CHUNK_SIZE = 512
        
    def test_write_and_list(self):
    	"""
        The test iterates over a specified count, 
        creating temporary objects with random data of random size.
        It writes these objects to the device,
        and then retrieves the list of objects using the objl method.
        The function performs a series of assertions to validate that the count
        of the retrieved list match the expected values based on the written objects.
        """
        count = 10
        obj_names = []
        for oid in xrange(1, count + 1):
            size = random.randint(1, 1024*1024)
            with data(size) as input:
                self.device.objw(input) # object_id matches tmp file name
                obj_names.append(input)
        content = self.device.objl()
        lines = content.split('\n')
        print("len is:" + str(len(lines)))
        assert len(lines) - 1 == count
        for oid in xrange(0, count):
        	line = lines[oid].split(' ')
        	print(str(line[0]) + str(int(line[1])))
        	assert line[0] in obj_names
        print("objects_via_ioctl: test_list finished successfully")
        self.cleanup(obj_names)
    	
    	
    def test_delete(self):
    	"""
        The test writes an object then reads and deletes it, after deletion
        another read attempt is made testing for an exception to make sure
        a read on a deleted object causes an exception to occur.
        """
        size = random.randint(1, 1024*1024)
        with data(size) as input:
            self.device.objw(input)
            self.device.objr(input)
            self.device.objd(input)
            try:
            	self.device.objr(input)
            except:
                print("objects_via_ioctl: test_delete finished successfully")
            	return
            else:
                raise Exception("No exception when trying to read deleted object")
            
    
    def test_write_read_and_compare(self):
        """
        The test iterates over a specified count, 
        creating temporary objects with random data of random size.
        It writes these objects to the device.
        The function performs a series of assertions to validate that the 
        contents of the read object matches the expected content of the written object.
        """
        count = 10
        obj_names = []
        for oid in xrange(1, count + 1):
            size = random.randint(1, 1024*1024)
            with data(size) as input:
                self.device.objw(input) # object_id matches tmp file name
                os.rename(input, input + "cpy")
                obj_names.append(input)
                with open(input + "cpy", 'rb') as src, open(input, 'wb') as dest:
                    left = src.read().ljust(self.align_size(size), '\0')
                    right = self.device.objr(input)
                    print(len(left))
                    print(len(right))
                    assert left == right
        print("objects_via_ioctl: test_read finished successfully")
        self.cleanup(obj_names)
    
    
    def cleanup(self, objects):
    	"""
    	This helper function handles the deletion of a given list of objects
    	to restore the device to its initial state between tests
    	"""
    	for obj in objects:
    		self.device.objd(obj)
    
    
    def align_size(self, size):
        """
        This helper function calculates the amount of blocks we should expect
        based on the size of the data chunks the driver uses
        """
        return int(math.ceil(size/float(self.CHUNK_SIZE))) * self.CHUNK_SIZE

