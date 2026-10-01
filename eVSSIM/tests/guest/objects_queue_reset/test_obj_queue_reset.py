import os
import time
import signal
import tempfile
import subprocess


DEVICE_NAME = '/dev/nvme0n1'
CTRL_RESET = '/sys/class/nvme/nvme0/reset_controller'
NVME_CLI_DIR = '/home/esd/guest'
OBJ_FEATURE_ID = 192

WRITERS = 4
OBJECT_SIZE = 4 * 1024
# Rewrites don't free old pages; this caps a run at 160 MiB of the 640 MiB
# device while still outlasting the resets
WRITES_PER_WRITER = 10000
RESETS = 5


def nvme(*args):
    subprocess.check_call(['./nvme'] + list(args), cwd=NVME_CLI_DIR)


def set_obj_feature(value):
    nvme('set-feature', DEVICE_NAME, "-f", str(OBJ_FEATURE_ID),
         "--value=%d" % value)


def objw(path):
    nvme('objw', DEVICE_NAME, path, "-offset", "0")


def random_file():
    result = tempfile.NamedTemporaryFile()
    result.write(os.urandom(OBJECT_SIZE))
    result.flush()
    return result


def wait_for_device(timeout=30):
    deadline = time.time() + timeout
    while not os.path.exists(DEVICE_NAME):
        assert time.time() < deadline, "%s did not come back" % DEVICE_NAME
        time.sleep(0.5)


def create_objects():
    """
    One object per writer, as the file name is the object key. Each is written
    once here, serially, as nvme-cli creates its key db (keys.db) racily.
    """
    sources = []
    for _ in xrange(WRITERS):
        src = random_file()
        objw(src.name)
        sources.append(src)
    return sources


def start_writers(sources):
    # Writes fail while a reset removes the device; count only successes
    loop = ('n=0; while [ $n -lt %d ]; do '
            './nvme objw %s %%s -offset 0 >/dev/null 2>&1 && n=$((n+1)); done'
            % (WRITES_PER_WRITER, DEVICE_NAME))
    return [subprocess.Popen(loop % src.name, shell=True, cwd=NVME_CLI_DIR,
                             preexec_fn=os.setsid)
            for src in sources]


def stop_writers(writers):
    # Kill each writer's process group, so no objw is left running
    for writer in writers:
        if writer.poll() is None:
            os.killpg(writer.pid, signal.SIGKILL)
            writer.wait()


def reset_repeatedly(writers):
    for _ in xrange(RESETS):
        time.sleep(1)
        assert any(w.poll() is None for w in writers), \
            "writers finished before all resets were done"
        with open(CTRL_RESET, 'w') as reset:
            reset.write('1')
        wait_for_device()


def check_write_read():
    with random_file() as check:
        with open(check.name, 'rb') as written:
            expected = written.read()
        objw(check.name)
        nvme('objr', DEVICE_NAME, check.name)
        with open(check.name, 'rb') as read_back:
            assert expected == read_back.read()
        nvme('objd', DEVICE_NAME, check.name)


class test_ObjQueueReset(object):

    def test_reset_during_object_writes(self):
        """
        Test: Reset with object writes in flight
        A reset deletes the I/O queues and disables the controller; both must
        wait for in-flight object commands. Writers run back to back so a reset
        is likely to land mid-command. Writes racing a reset may fail.
        """
        set_obj_feature(1)
        sources = create_objects()
        writers = start_writers(sources)
        try:
            reset_repeatedly(writers)
        finally:
            stop_writers(writers)

        check_write_read()

        for src in sources:
            subprocess.call(['./nvme', 'objd', DEVICE_NAME, src.name],
                            cwd=NVME_CLI_DIR)
        set_obj_feature(0)
        print("objects_queue_reset: test_reset_during_object_writes finished successfully")
