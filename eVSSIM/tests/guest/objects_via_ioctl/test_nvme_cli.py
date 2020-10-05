import os
import random
import tempfile
import subprocess
import contextlib
import collections
import unittest
from nose.plugins.attrib import attr

BLOCK_SIZE = 512
MAGIC = 'eVSSIM_MAGIC{object_id}_{partition_id}'

MetaData = collections.namedtuple('MetaData', ['path', 'size'])

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

@contextlib.contextmanager
def metadata(object_id, partition_id):
    """
    Provides a context manager that returns a MetaData object describing
    an on disk metadata file who's lifetime is limited to the scope of the
    context manager.

    :param object_id: The id of the object this metadata describes.
    :param partition_id: The id of the partition on which this object resides.
    """
    md = MAGIC.format(object_id=object_id, partition_id=partition_id)
    with tempfile.NamedTemporaryFile() as result:
        result.write(md)
        yield MetaData(path=result.name, size=len(md))

class NvmeDevice(object):

    _partition = random.randint(10 ** 2, 10 ** 5)  

    def __init__(self, device='/dev/nvme0n1', nvme_cli_dir='/home/esd/guest'):
        self.device = device
        self.nvme_cli_dir = nvme_cli_dir

    @staticmethod
    def partition():
        """
        Returns an incrementing partition id.
        The value is seeded with a random number so conflicts should be rare
        (although currently there is no good way to ensure this).
        """
        NvmeDevice._partition += 1
        return NvmeDevice._partition

    def read(self, metadata, size):
        """
        Read (sector-based) the contents specified in the metadata.

        :param metadata: MetaData object.
        :param size: Size of the data to read from the given device.
        :return: The contents of the requested data.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
                './nvme', 'read', self.device,
                '--block-count=' + str(blocks(size)),
                '--data-size=' + str(size),
                '--data=' + dest.name,
                '--metadata=' + metadata.path,
                '--metadata-size=' + str(metadata.size),
            ], cwd=self.nvme_cli_dir)
            return dest.read()

    def write(self, metadata, data):
        """
        Write (sector-based) the given data to this device.

        :param metadata: MetaData object.
        :param data: Path to file containing data to write.
        """
        size = os.path.getsize(data)
        subprocess.check_call([
            './nvme', 'write', self.device,
            '--block-count=' + str(blocks(size)),
            '--data-size=' + str(size),
            '--data=' + data,
            '--metadata=' + metadata.path,
            '--metadata-size=' + str(metadata.size),
        ], cwd=self.nvme_cli_dir)

    def create_partition(self, requested_partition_id=0):
        """
        Allocates and initalize a new partition

        :param requested_partition_id: specifies the unique identifier of the partition to create, if contain 0 (default) any partition_id may be assigned
        :return the newly created partition's partition_id
        """
        subprocess.check_output([
            './nvme', 'create-partition', self.device,
            '--partition-id=' + str(requested_partition_id)
        ], cwd=self.nvme_cli_dir)

    def create_object(self, partition_id, requested_object_id=0):
        """
        Allocate and initialize one user object. 

        :param partition_id: unique identifier of the partition to create the newly object into, if the partition_id doesn't exist, the request fails. 
        :param requested_object_id: specifies the unique identifier of the object to create, if contain 0 (default) any object_id may be assigned
        :return the newly created object' object_id 
        """
        subprocess.check_output([
            './nvme', 'create-object', self.device,
            '--partition-id=' + str(partition_id), 
            '--requested-object-id=' + str(requested_object_id)
        ], cwd=self.nvme_cli_dir)

    def create_and_write_object(self, partition_id, requested_object_id, data_out): 
        """
        Allocate and initialize one user object and then write data to the newly created user object.

        :param partition_id: unique identifier of the partition to create the newly object into, if the partition_id doesn't exist, the request fails. 
        :param requested_object_id: specifies the unique identifier of the object to create, if contain 0 any object_id may be assigned
        :param data_out: path to a file containing data (DATA-OUT Buffer) (todo add default-behavior)

        :return the newly created object' object_id 
        """
        size = os.path.getsize(data_out)
        subprocess.check_output([
            './nvme', 'create-and-write-object', self.device,
            '--partition-id=' + str(partition_id), 
            '--requested-object-id=' + str(requested_object_id),
            '--data-size=' + str(size),
            '--data=' + data_out
        ], cwd=self.nvme_cli_dir)                

    def read_object(self, partition_id, object_id, length, data_in=""): 
        """
        Request that the device return data from the specified user object.

        :param partition_id: unique identifier of the partition where the object reside
        :param object_id: unique identifier of the object to read
        :param data_in: path to a file that will hold the returned data (DATA-IN Buffer) 
        :param length: specifies the number of bytes to be read 
        """
        if(data_in):
            subprocess.check_call([
                './nvme', 'read-object', self.device,
                '--partition-id=' + str(partition_id), 
                '--object-id=' + str(object_id),
                '--data=' + data_in.name,
                '--length=' + str(length),
            ], cwd=self.nvme_cli_dir)
        else:
            with tempfile.NamedTemporaryFile(mode='rb') as tmp:
                subprocess.check_call([
                    './nvme', 'read-object', self.device,
                    '--partition-id=' + str(partition_id), 
                    '--object-id=' + str(object_id),
                    '--data=' + tmp.name,
                    '--length=' + str(length),
                ], cwd=self.nvme_cli_dir)
                return dest.read()

    def write_object(self, partition_id, object_id, data_out=""):
        """
        Request to write data to the specified user object.

        :param partition_id:unique identifier of the partition where the object reside 
        :param requested_object_id: unique identifier of the object to write into
        :param data_out: path to a file containing data (DATA-OUT Buffer), if no data_out specified the object is  overwritten with zero data, effectively deleting its data 
        """
        if(data_out):
            size = os.path.getsize(data_out)
            subprocess.check_output([
                './nvme', 'write-object', self.device,
                '--partition-id=' + str(partition_id), 
                '--object-id=' + str(object_id),
                '--data-size=' + str(size),
                '--data=' + data_out
            ], cwd=self.nvme_cli_dir)
        else:
            with tempfile.NamedTemporaryFile() as tmp:
                subprocess.check_call([
                    './nvme', 'write-object', self.device,
                    '--partition-id=' + str(partition_id), 
                    '--object-id=' + str(object_id),
                    '--data-size=' + str(0),
                    '--data=' + tmp.name,
                ], cwd=self.nvme_cli_dir)

    def remove_object(self, partition_id, object_id):
        """
        Deletes a user object.

        :param partition_id: unique identifier of the partition where the object reside
        :param object_id: unique identifier of the object to delete
        """
        subprocess.check_call([
            './nvme', 'remove-object', self.device,
            '--partition-id=' + str(partition_id), 
            '--object-id=' + str(object_id),
        ], cwd=self.nvme_cli_dir)

class TestNvmeCli(unittest.TestCase):
    """
    Class containing tests for nvme-cli using objects.
    """    
    
    device = NvmeDevice() # todo, move to setup
    partition = NvmeDevice.partition() 

    def test_random(self):
        """
        Test writing and reading random data to a random partition (sanity check)
        """
        size = 10 ** 3 # todo, make it random
        count = 10 ** 2  
        for oid in xrange(1, count+1):
            with metadata(oid, self.partition) as md, data(size) as random_data:
                self.device.write(md, random_data)
                with open(random_data, 'rb') as src:
                    assert src.read() == self.device.read(md, size)

    def test_create_partition_unspecified(self):
        """
        Tests creating a partition with unspecified id.
        """
        assert self.device.create_partition()

    def test_create_partition_specified(self):
        """
        Tests creating a partition with specified id.
        """
        assert self.device.create_partition(self.device.partition())

    def test_create_duplicate_partition(self):
        """
        Tests trying to create a partition with already existing id.
        """
        partition_id = self.device.create_partition(NvmeDevice.partition())
        self.assertNotEqual(partition_id, self.device.create_partition(partition_id))

    def test_create_empty_object_unspecified(self): 
        """
        Test creating a new empty object into a random partition without specifying object_id 
        """
        assert self.device.create_object(self.device.create_partition())

    def test_create_empty_object_specified(self):
        """
        Test creating a new empty object into a random partition asking for a specific object_id 
        """
        object_id = random.randint(10 ** 1, 10 ** 5)
        self.assertEqual(object_id, self.device.create_object(self.device.create_partition(),object_id))

    def test_create_duplicate_object(self):
        """
        Test trying to create an object with already existing id in the same partition.
        """
        partition_id = self.device.create_partition()
        object_id = self.device.create_object(partition_id)
        self.assertFalse(self.device.create_object(partition_id, object_id))

    def test_create_dup_object_diff_partition(self):
        """
        Test creating an object with already existing id in different partition.
        """
        partition_id1 = self.device.create_partition()
        partition_id2 = self.device.create_partition()
        object_id1 = self.device.create_object(partition_id1)
        object_id2 = self.device.create_object(partition_id2, object_id1)
        self.assertEqual(object_id1, object_id2)

    def test_create_object_no_partition(self):
        """
        Test trying to create an object inside a non-existing partition.
        """
        partition_id = self.device._partition + 1
        self.assertFalse(self.device.create_object(partition_id))

    def test_create_and_write_object(self):
        """
        Test creating a new object into a random partition and writing into it some random data (one action)
        """
        partition_id = self.device.create_partition()
        size = random.randint(10 ** 2, 10 ** 4)
        with data(size) as random_data:
            assert self.device.create_and_write_object(partition_id, 0, random_data)

    def test_create_then_write(self):
        """
        Test creating a new empty object into a random partition, then writing into it some random data (two actions)
        """
        partition_id = self.device.create_partition()
        object_id = self.device.create_object(partition_id)
        size = random.randint(10 ** 2, 10 ** 4)
        with data(size) as random_data:
            assert self.device.write_object(partition_id, object_id, random_data)

    def test_create_and_write_then_read(self): 
        """
        Test creating a new object into a random partition with random data, then reading it.
        """

        partition_id = self.device.create_partition()
        size = random.randint(10 ** 2, 10 ** 4)
        with data(size) as random_data:  
                object_id = self.device.create_and_write_object(partition_id, 0, random_data)
                with open(random_data, 'rb') as src:
                    assert src.read() == self.device.read_object(partition_id, object_id,size)

    def test_create_remove(self):
        """
        Test creating a new object into a random partition with random data and then deleting it.
        """
        partition_id = self.device.create_partition()
        object_id = self.device.create_object(partition_id)
        assert self.device.remove_object(partition_id, object_id)

    def test_remove_non_exist(self):
        """
        Test removing a non-existent object
        """
        partition_id = random.randint(10 ** 2, 10 ** 5)
        object_id = random.randint(10 ** 2, 10 ** 5)
        self.assertFalse(self.device.remove_object(partition_id, object_id))

    def test_write_zero_data(self):
        """
        Test creating and writing an object with random data, then writing zero data into it effectively erasing its data.
        """
        partition_id = self.device.create_partition()
        size = random.randint(10 ** 2, 10 ** 4)
        with data(size) as random_data:  
                object_id = self.device.create_and_write_object(partition_id, 0, random_data)
                with data(0) as zero_data, open(random_data,'rb') as src:
                    self.device.write_object(partition_id, object_id)
                    self.assertNotEqual (self.device.read_object(partition_id, object_id, size), src.read())