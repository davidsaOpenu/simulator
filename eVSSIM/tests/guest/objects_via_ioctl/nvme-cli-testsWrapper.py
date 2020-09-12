#
# This short script is intended to check the nvme-cli's ability to read written data and verify its integrity.
# There are a total of 5 tests, where each test performs several iterations of the same write / read / compare action sequence, but with different object sizes (and IDs)
# The script should be run from inside the directory where the patched nvme util is installed.
#

import os
import sys
from random import randint

write_command_template = "../nvme write /dev/nvme0n1 --block-count=%d --data-size=%d --data=%s --metadata-size=%d --metadata=%s"  # --show-command"
read_command_template = "../nvme read /dev/nvme0n1 --block-count=%d --data-size=%d --data=%s --metadata-size=%d --metadata=%s"  # --show-command"
nvme_block_size = 512


def calculate_block_count(dataSize):
    initialCount = dataSize / nvme_block_size
    remainder = dataSize % nvme_block_size

    # smaller than block size -> use one block only (0 indicates we will use 1 block, as qemu adds 1 to the block count)
    if initialCount == 0:
        return 0
    elif remainder == 0:
        return initialCount - 1  # size is a precise multiplication of block size
    else:
        return initialCount  # size is at initialCount blocks + 1


def executeSingleTest(data_size, count, partition_id):

    block_count = calculate_block_count(data_size)

    for object_id in xrange(1, count+1):
        # we're generating random data for EACH object
        random_data = os.urandom(data_size)
        tmpInFile = 'tmp_%d.in' % data_size
        with open(tmpInFile, 'w') as data_file:
            data_file.write(random_data)

        metadata_data = 'eVSSIM_MAGIC%s_%d!' % (partition_id, object_id)
        metadata_size = len(metadata_data)
        metadata_file = 'metadata.md'
        with open(metadata_file, 'wb') as metadata_fileHandler:
            metadata_fileHandler.write(metadata_data)

        write_command = write_command_template % (
            block_count, data_size, tmpInFile, metadata_size, metadata_file)
        print '[%d/%d] writing object of size %d' % (object_id, count, data_size)
        os.system(write_command)

        tmpOutFile = 'tmp_%d.out' % data_size
        read_command = read_command_template % (
            block_count, data_size, tmpOutFile, metadata_size, metadata_file)
        print '[%d/%d] reading object of size %d' % (object_id, count, data_size)
        os.system(read_command)

        with open(tmpOutFile, 'rb') as tmpOutFileHandle:
            inputByteArray = tmpOutFileHandle.read()
            os.remove(tmpOutFile)
            os.remove(tmpInFile)
            os.remove(metadata_file)

        if inputByteArray == random_data:
            print '[%d/%d] write + read %d bytes SUCCESSFUL' % (object_id, count, data_size)
        else:
            print '[%d/%d] write + read %d bytes FAILED - terminating!' % (object_id, count, data_size)
            return False

    return True

    # perform total time + avg time for each run (by dividing at the end)

# we're using a random partition id as we want to avoid a situtation where this util fails in different executions because of using the same partition id -> this minimizes the chance of that happening


os.system("sudo rmmod nvme; sudo insmod $(sudo find / -name 'nvme*ko') strategy=1;")

part_id = randint(100, 9999999)
print "partition id is: " + str(part_id)

if executeSingleTest(10, 10, part_id) and executeSingleTest(512, 10, part_id+1) and executeSingleTest(1000, 100, part_id+2) and executeSingleTest(10000, 100, part_id+3) and executeSingleTest(10000, 1000, part_id+4):
    print "Finished all tests successfully!"
