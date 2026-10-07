#!/usr/bin/env python3
"""Synthetic event stream for the ELK validity tests, 16 MB user-data tier.

  event_generator.py <stream_path> <tally_path>

One writer per (disk, channel, die). Writers take turns, one operation each,
until 16 MiB of user data (4 KiB page writes) were emitted. There is no
randomness: the stream is a pure function of the constants below. Each writer
owns its counters and clock, so running writers in parallel changes only the
interleaving, never what a writer emits.

The stream is one event per line, replayed through the real log manager by
event_generator_tests.cc:
  disk type channel die block page start_us end_us
The tally (events emitted per disk and type) is the ground truth.
"""
import collections
import itertools
import json
import os
import sys

DISKS = 3
CHANNEL_NB = 4
DIES = 8  # die = flash index, channel = die % CHANNEL_NB, as in ssd_io_manager.c
BLOCK_NB = 64
PAGE_NB = 64
PAGE_SIZE = 4096
USER_BYTES = 16 * 2**20
BASE_US = 1767225600000000  # 2026-01-01 00:00:00 UTC
DURATION_US = {"write": 900, "read": 50, "erase": 2000}
CYCLE = ("write", "read", "erase")
EVENTS = {
    "write": ("PhysicalCellProgramLog", "LogicalCellProgramLog"),
    "read": ("PhysicalCellReadLog",),
    "erase": ("BlockEraseLog",),
}


class Writer:
    def __init__(self, disk, die):
        self.disk = disk
        self.die = die
        self.channel = die % CHANNEL_NB
        self.ops = 0
        self.clock_us = BASE_US

    def next_op(self):
        op = CYCLE[self.ops % len(CYCLE)]
        block, page = self.ops // PAGE_NB % BLOCK_NB, self.ops % PAGE_NB
        start, self.clock_us = self.clock_us, self.clock_us + DURATION_US[op]
        self.ops += 1
        return op, block, page, start, self.clock_us


def generate(stream):
    """Write the stream, return the tally: disk -> event type -> count."""
    tally = collections.defaultdict(collections.Counter)
    writers = [Writer(disk, die) for disk in range(DISKS) for die in range(DIES)]
    writes = 0
    for w in itertools.cycle(writers):
        if writes == USER_BYTES // PAGE_SIZE:
            break
        op, block, page, start, end = w.next_op()
        writes += op == "write"
        for event in EVENTS[op]:
            stream.write("%d %s %d %d %d %d %d %d\n" % (w.disk, event, w.channel, w.die, block, page, start, end))
            tally[str(w.disk)][event] += 1
    return tally


def main(stream_path, tally_path):
    with open(stream_path, "w") as stream:
        tally = generate(stream)
    os.makedirs(os.path.dirname(tally_path), exist_ok=True)
    with open(tally_path, "w") as f:
        json.dump(tally, f, indent=2, sort_keys=True)


if __name__ == "__main__":
    main(*sys.argv[1:])
