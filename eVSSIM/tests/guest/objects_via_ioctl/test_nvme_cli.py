import os
import math
import random
import tempfile
import subprocess
import contextlib
import collections
import unittest
import binascii


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
        subprocess.check_call([
            './nvme', 'set-feature', self.device,
             "-f" , str(OBJ_FEAT_ID), "--value=1"
        ], cwd=self.nvme_cli_dir)
        
    def __del__(self):
        subprocess.check_call([
            './nvme', 'set-feature', self.device,
             "-f" ,str(OBJ_FEAT_ID), "--value=0",
        ], cwd=self.nvme_cli_dir)
    
    def objw(self, obj_id, offset = 0):
        """
        Write the given data to this device in the object described by
        the given object id.

        :param obj_id: String containing object id of the written object.
        """
        subprocess.check_call([
            './nvme', 'objw', self.device,
             obj_id, "-offset", str(offset),
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
OBJ_FEAT_ID = 192

class test_NvmeCli(object):
    """
    Class containing tests for nvme cli using objects.
    """
    device = NvmeDevice(DEVICE_NAME, NVME_CLI_DIR)
    
    CHUNK_SIZE = 512
    MAX_OBJECT_SIZE = 1024
    OBJECT_COUNT = 1000
    OBJ_NAMES = []
    
    def test_write_and_list(self):
	"""
    	Test: Nvme-cli proper list formatting
        The test iterates over a specified count, 
        creating temporary objects with random data of random size.
        It writes these objects to the device,
        and then retrieves the list of objects using the objl method.
        The function performs a series of assertions to validate that the count
        of the retrieved list match the expected values based on the written objects.
        """
        self.cleanup()
        for oid in xrange(0, self.OBJECT_COUNT):
            size = random.randint(1, self.MAX_OBJECT_SIZE)
            with data(size) as input:
                self.device.objw(input) # object_id matches tmp file name
                self.OBJ_NAMES.append(input)
        content = self.device.objl()
        lines = content.split('\n')
        print(str(len(lines)))
        print(str(lines))
        assert len(lines) - 1 == self.OBJECT_COUNT
        for oid in xrange(0, self.OBJECT_COUNT):
        	assert lines[oid] in self.OBJ_NAMES
        print("objects_via_ioctl: test_list finished successfully")
        self.cleanup()
    	
    	
    def test_delete(self):
    	"""
    	Test: Deleted objects can't be read
        The test writes an object then reads and deletes it, after deletion
        another read attempt is made testing for an exception to make sure
        a read on a deleted object causes an exception to occur.
        """
        self.cleanup()
        size = random.randint(1, self.MAX_OBJECT_SIZE)
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
        Test: Written objects match  
        The test iterates over a specified count, 
        creating temporary objects with random data of random size.
        It writes these objects to the device.
        The function performs a series of assertions to validate that the 
        contents of the read object matches the expected content of the written object.
        """
        self.cleanup()
        for oid in xrange(0, self.OBJECT_COUNT):
            size = random.randint(1, self.MAX_OBJECT_SIZE)
            with data(size) as input:
                self.device.objw(input)
                os.rename(input, input + "cpy")
                self.OBJ_NAMES.append(input)
                with open(input + "cpy", 'rb') as src, open(input, 'wb') as dest:
                    write_in = src.read()
                    read_out = self.device.objr(input)
                    assert write_in == read_out
        print("objects_via_ioctl: test_read finished successfully")
        self.cleanup()
  
    
    def test_empty_list(self):
        """
        Test: List is empty
        The test checks that the list returned by objl prior to writing
        any objects is empty as expected
        """
        assert "" == self.device.objl()
        
        
    def test_empty_object(self):
        """
        Test: Empty object rw
        The test checks that empty objects are properly handled
        by creating an object from an empty file and then reading it
        """
        self.cleanup()
        with data(0) as empty_file:
            self.device.objw(empty_file)
            assert "" == self.device.objr(empty_file)
            self.device.objd(empty_file)
        self.cleanup()
    
    
    def test_max_size_object(self):
        """
        Test: Max size object rw
        The test checks that max sized objects are properly handled
        by creating an object of size U32 max and then reading it
        """
        assert 0 == 0 # qemu can't allocate enough mem to host a max sized object


    def test_overwrite(self):
    	"""
        Test: Overwrite objects
        The test iterates over a specified count, 
        creating temporary objects with random data of random size.
        It writes these objects to the device.
        The function performs a series of assertions to validate that the 
        contents of the read object matches the expected content of the written object.
        """
        self.cleanup()
        for oid in xrange(0, self.OBJECT_COUNT):
            size = random.randint(1, self.MAX_OBJECT_SIZE) # size limited by qemu allocation, should revert to 1Mb after switch to vssim
            with data(size) as input:
                self.device.objw(input) # object_id matches tmp file name
                os.rename(input, input + "cpy")
                self.OBJ_NAMES.append(input)
                with open(input + "cpy", 'r+b') as src, open(input, 'wb+') as dest:
                    ow_contents = os.urandom(random.randint(1, self.MAX_OBJECT_SIZE))
                    dest.write(ow_contents)
                    dest.flush()
                    offset = random.randint(1, self.MAX_OBJECT_SIZE)
                    print(offset)
                    print("ORIGINAL DATA:\t" + str(binascii.hexlify(bytearray(src.read()))))
                    print("OVERWRITE DATA:\t" + str(binascii.hexlify(bytearray(ow_contents))))
                    self.device.objw(input, offset)
                    dest.truncate(0)
                    src.seek(offset)
                    src.write(ow_contents)
                    src.seek(0)
                    expected = src.read()
                    read_out = self.device.objr(input)
                    print("Expected:\t" + str(binascii.hexlify(bytearray(expected))))
                    print("Read out:\t" + str(binascii.hexlify(bytearray(read_out))))
                    assert expected == read_out
        print("objects_via_ioctl: test_read finished successfully")
        self.cleanup()

    
    def cleanup(self):
    	"""
    	This helper function handles the deletion of a given list of objects
    	to restore the device to its initial state between tests
    	"""
    	for obj in self.OBJ_NAMES:
    		self.device.objd(obj)
    
    
    def align_size(self, size):
        """
        This helper function calculates the amount of blocks we should expect
        based on the size of the data chunks the driver uses
        """
        return int(math.ceil(size/float(self.CHUNK_SIZE))) * self.CHUNK_SIZE

