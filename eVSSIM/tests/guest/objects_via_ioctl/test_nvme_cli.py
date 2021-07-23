import os
import random
import tempfile
import subprocess
import contextlib
import collections
import pytest


BLOCK_SIZE = 512
MAGIC = 'eVSSIM_MAGIC{object_id}_{partition_id}!'


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
        result.flush()
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


DEVICE_NAME = '/dev/nvme0n1'


@pytest.fixture()
def device(request):
    return NvmeDevice(DEVICE_NAME, request.config.getoption('--nvme-cli-dir'))


@pytest.fixture(params=[(BLOCK_SIZE * 2) * i for i in xrange(2, 6)])
def size(request):
    return request.param


@pytest.fixture(params=[10 ** i for i in xrange(3)])
def count(request):
    return request.param


class TestNvmeCli(object):
    """
    Class containing tests for nvme cli using objects.
    """

    def test_random(self, device, size, count):
        """
        Test writing and reading random data to a random partition.
        """
        os.system(
            "sudo rmmod nvme; sudo insmod $(sudo find / -name 'nvme*ko') strategy=1;")

        partition = NvmeDevice.partition()

        for oid in xrange(1, count + 1):
            with metadata(oid, partition) as md, data(size) as input, data(size / 2) as m_input:
                device.write(md, input)
                with open(input, 'rb+') as src, open(m_input, 'rb') as m_src:
                    assert src.read() == device.read(md, size)
                    src.seek(0)
                    src.write(m_src.read())
                    device.write(md, m_input)
                    src.seek(0)
                    assert src.read() == device.read(md, size)
