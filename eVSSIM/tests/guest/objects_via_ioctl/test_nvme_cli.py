import os
import random
import tempfile
import subprocess
import contextlib
import collections
import unittest


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

    def __init__(self, device, nvme_cli_dir):
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
        Read the contents of the object specified in the metadata.

        :param metadata: MetaData object describing the object in
            this device from which to read.
        :param size: Size of the data to read from the given device.
        :return: The contents of the requested object.
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
        Write the given data to this device in the object described by
        the given metadata.

        :param metadata: MetaData object describing the object on this
            device to write to.
        :param data: Path to file containing data to write to the specified object.
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
        
    def objw(self, obj_id, metadata, data):
        """
        Write the given data to this device in the object described by
        the given metadata.

        :param metadata: MetaData object describing the object on this
            device to write to.
        :param data: Path to file containing data to write to the specified object.
        """
        size = os.path.getsize(data)
        subprocess.check_call([
            './nvme', 'objw', self.device,
             obj_id,
            '--data-size=' + str(size),
            '--data=' + data,
        ], cwd=self.nvme_cli_dir)
        
    def objr(self, obj_id, metadata, size):
        """
        Write the given data to this device in the object described by
        the given metadata.

        :param metadata: MetaData object describing the object on this
            device to write to.
        :param data: Path to file containing data to write to the specified object.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
                './nvme', 'objr', self.device,
                obj_id,
                '--data-size=' + str(size),
                '--data=' + dest.name,
            ], cwd=self.nvme_cli_dir)
            return dest.read()

    def objl(self, size):
        """
        Write the given data to this device in the object described by
        the given metadata.

        :param metadata: MetaData object describing the object on this
            device to write to.
        :param data: Path to file containing data to write to the specified object.
        """
        with tempfile.NamedTemporaryFile(mode='rb') as dest:
            subprocess.check_call([
            './nvme', 'objl', self.device, 
            '--data-size=' + str(size),
            '--data=' + dest.name,
        ], cwd=self.nvme_cli_dir)
            return dest.read()

DEVICE_NAME = '/dev/nvme0n1'
NVME_CLI_DIR = '/home/esd/guest'

class test_NvmeCli(object):
    """
    Class containing tests for nvme cli using objects.
    """
    def test_random(self):
        """
        Test writing and reading objects containing random data
        """
        device = NvmeDevice(DEVICE_NAME, NVME_CLI_DIR)
        partition = NvmeDevice.partition()
        count = 10
        size = 1024*1024
        for oid in xrange(1, count + 1):
            with metadata(oid, partition) as md, data(size) as input:
                device.objw(input, md, input)
                with open(input, 'rb') as src:
                    assert src.read() == device.objr(input, md, os.path.getsize(input))
        print("objects_via_ioctl finished successfully")
