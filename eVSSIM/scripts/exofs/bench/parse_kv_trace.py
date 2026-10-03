#!/usr/bin/env python
"""T4 - turn an exofs KV ftrace dump into the object inventory table.

Consumes the trace.txt / inode_map.txt pair written by object_inventory_workload.sh
and emits:

  * a CSV of every NVMe Key-Value command (op, type, object id, attr, length, phase)
  * a per-type summary table, in markdown, ready for the inventory document
  * a per-phase breakdown, which is what answers "which object types have parts
    read or written repeatedly"

Runs on python 2.7 (the Trusty guest) and python 3.
"""

from __future__ import print_function

import argparse
import csv
import math
import os
import re
import sys
from collections import OrderedDict, defaultdict

# fs/exofs/common.h
EXOFS_OBJ_OFF = 0x10000
EXOFS_SUPER_ID = 0x10000
EXOFS_DEVTABLE_ID = 0x10001
EXOFS_ROOT_ID = 0x10002

# super.c:48. Some call sites (exofs_set_obj_data, exofs_delete_obj) OR this bit
# into the key themselves and pass is_attrib=false, so the traced attr flag is 0
# even though the target is the attribute object. Both encodings must normalise
# to the same thing or those commands land against an object id that is not in
# the inode map and go unclassified.
NVME_ATTR_KEY_HIGH = 1 << 31

# In this fork the object id IS the inode number: exofs_new_inode() assigns
# inode->i_ino = s_nextid++ + EXOFS_OBJ_OFF (inode.c) and exofs_oi_objno()
# returns i_ino unchanged (exofs.h), so the offset is already baked into the
# inode number. Mainline exofs added EXOFS_OBJ_OFF at object-id time instead --
# subtracting it here would shift every lookup by 64K and classify nothing.

RECORD_RE = re.compile(
    r"exofs_kv\s+op=(?P<op>\w+)\s+key=0x(?P<key>[0-9a-fA-F]+)\s+"
    r"attr=(?P<attr>[01])\s+req=(?P<req>\d+)\s+len=(?P<len>\d+)\s+"
    r"ret=(?P<ret>-?\d+)"
)
MARKER_RE = re.compile(r"tracing_mark_write:\s*PHASE=(?P<phase>\S+)")

RETRIEVE_OPS = ("retrieve",)
STORE_OPS = ("store",)
DELETE_OPS = ("delete",)
EXIST_OPS = ("exist",)


def load_inode_map(path):
    # type: (str) -> dict
    """inode number -> one of 'file', 'directory', 'symlink', 'other'."""
    kinds = {"f": "file", "d": "directory", "l": "symlink"}
    inode_map = {}

    if not path or not os.path.isfile(path):
        return inode_map

    with open(path) as handle:
        for line in handle:
            fields = line.split(None, 3)
            if len(fields) < 2:
                continue
            try:
                ino = int(fields[0])
            except ValueError:
                continue
            token = fields[1]
            # find -printf '%y' gives a single letter; the stat fallback gives a word.
            inode_map[ino] = kinds.get(token, token.lower())

    return inode_map


def classify(object_id, is_attr, inode_map):
    # type: (int, bool, dict) -> str
    """Name the object a KV command touched.

    An object's attribute is its exofs_fcb -- the inode metadata -- not an
    attribute of its data, so per-inode objects are labelled accordingly.
    """
    if object_id & NVME_ATTR_KEY_HIGH:
        object_id &= ~NVME_ATTR_KEY_HIGH
        is_attr = True

    if object_id == EXOFS_SUPER_ID:
        return "superblock attribute" if is_attr else "superblock"
    if object_id == EXOFS_DEVTABLE_ID:
        return "device table attribute" if is_attr else "device table"

    if object_id == EXOFS_ROOT_ID:
        kind = "directory"
    elif object_id >= EXOFS_OBJ_OFF:
        kind = inode_map.get(object_id)
    else:
        return "unknown (id below EXOFS_OBJ_OFF)"

    if kind is None:
        # Deleted before the map was captured, or reached by a path we did not
        # walk. Keep it visible rather than silently dropping it.
        return "unmapped inode attribute" if is_attr else "unmapped inode data"
    if kind == "directory":
        return "directory inode attribute" if is_attr else "directory data"
    if kind == "file":
        return "file inode attribute" if is_attr else "file data"

    return "%s inode attribute" % kind if is_attr else "%s data" % kind


def parse(trace_path, inode_map):
    # type: (str, dict) -> list
    rows = []
    phase = "pre-workload"

    with open(trace_path) as handle:
        for line in handle:
            marker = MARKER_RE.search(line)
            if marker:
                phase = marker.group("phase")
                continue

            match = RECORD_RE.search(line)
            if not match:
                continue

            object_id = int(match.group("key"), 16)
            is_attr = match.group("attr") == "1"
            if object_id & NVME_ATTR_KEY_HIGH:
                object_id &= ~NVME_ATTR_KEY_HIGH
                is_attr = True

            rows.append(OrderedDict((
                ("phase", phase),
                ("op", match.group("op")),
                ("type", classify(object_id, is_attr, inode_map)),
                ("object_id", "0x%x" % object_id),
                ("is_attr", int(is_attr)),
                ("requested", int(match.group("req"))),
                ("length", int(match.group("len"))),
                ("ret", int(match.group("ret"))),
            )))

    return rows


def stddev(values):
    # type: (list) -> float
    mean = sum(values) / float(len(values))
    return math.sqrt(sum((v - mean) ** 2 for v in values) / float(len(values)))


def summarise(rows):
    # type: (list) -> str
    by_type = defaultdict(list)
    for row in rows:
        by_type[row["type"]].append(row)

    lines = [
        "| object type | commands | retrieve | store | delete | exist | min size | max size | stddev |",
        "|---|---|---|---|---|---|---|---|---|",
    ]

    for name in sorted(by_type):
        entries = by_type[name]
        # Only successful commands describe a real stored size.
        sizes = [e["length"] for e in entries if e["ret"] == 0 and e["length"] > 0]
        n_ret = sum(1 for e in entries if e["op"] in RETRIEVE_OPS)
        n_sto = sum(1 for e in entries if e["op"] in STORE_OPS)
        n_del = sum(1 for e in entries if e["op"] in DELETE_OPS)
        n_exi = sum(1 for e in entries if e["op"] in EXIST_OPS)

        if not sizes:
            min_s = max_s = spread = "n/a"
        else:
            min_s, max_s = str(min(sizes)), str(max(sizes))
            if len(set(sizes)) == 1:
                spread = "fixed"
            elif name.startswith("file data"):
                # Per the DoD: size follows the workload, not the filesystem.
                spread = "N/A"
            else:
                spread = "%.1f" % stddev(sizes)

        lines.append("| %s | %d | %d | %d | %d | %d | %s | %s | %s |" % (
            name, len(entries), n_ret, n_sto, n_del, n_exi, min_s, max_s, spread))

    return "\n".join(lines)


def phase_breakdown(rows):
    # type: (list) -> str
    counts = defaultdict(lambda: defaultdict(int))
    phases = []
    for row in rows:
        if row["phase"] not in phases:
            phases.append(row["phase"])
        counts[row["phase"]][row["type"]] += 1

    lines = []
    for phase in phases:
        lines.append("")
        lines.append("### %s" % phase)
        for name in sorted(counts[phase], key=lambda k: -counts[phase][k]):
            lines.append("  %-34s %d" % (name, counts[phase][name]))

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", help="ftrace dump written by the workload script")
    parser.add_argument("--inode-map", help="inode_map.txt from the same run")
    parser.add_argument("--csv", help="write the per-command CSV here")
    args = parser.parse_args()

    if not os.path.isfile(args.trace):
        sys.exit("ERROR no such trace file: %s" % args.trace)

    inode_map = load_inode_map(args.inode_map)
    if args.inode_map and not inode_map:
        print("WARNING inode map is empty; per-inode objects stay unclassified",
              file=sys.stderr)

    rows = parse(args.trace, inode_map)
    if not rows:
        sys.exit("ERROR no exofs_kv records in %s -- was the kernel built with "
                 "CONFIG_EXOFS_KV_TRACE=y and tracing left on?" % args.trace)

    if args.csv:
        with open(args.csv, "w") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
            writer.writeheader()
            writer.writerows(rows)
        print("Wrote %s (%d commands)" % (args.csv, len(rows)))
        print("")

    failures = sum(1 for r in rows if r["ret"] != 0)

    print("## Object inventory (%d KV commands, %d failed)" % (len(rows), failures))
    print("")
    print(summarise(rows))
    print("")
    print("## Commands per workload phase")
    print(phase_breakdown(rows))


if __name__ == "__main__":
    main()
