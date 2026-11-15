#!/usr/bin/env python3
"""Validate labelled fio text with the guest suite's Python 3 runtime."""
from __future__ import print_function

import math
import re
import sys


def directions(job):
    modes = {
        "read": ("read",), "randread": ("read",),
        "write": ("write",), "randwrite": ("write",),
        "rw": ("write", "read"), "randrw": ("write", "read"),
    }
    required = set()
    with open(job) as stream:
        for line in stream:
            match = re.match(r"\s*rw\s*=\s*([^;#\s]+)", line)
            if match:
                mode = match.group(1)
                if mode not in modes:
                    raise ValueError("unsupported rw mode: " + mode)
                required.update(modes[mode])
    if not required:
        raise ValueError("missing rw setting in " + job)
    return [direction for direction in ("write", "read") if direction in required]


def iops(path, required):
    with open(path) as stream:
        text = stream.read()
    if any(int(value) for value in re.findall(r"\berr\s*=\s*(\d+)", text)):
        raise ValueError("fio reported an error in " + path)
    result = {}
    for direction in required:
        tokens = re.findall(r"^\s*" + direction +
                            r"\s*:\s*[^\n]*?\biops\s*=\s*([^,\s]+)",
                            text, re.IGNORECASE | re.MULTILINE)
        if len(tokens) != 1:
            raise ValueError("expected one " + direction + " IOPS value in " + path)
        match = re.match(r"^([0-9]+(?:\.[0-9]+)?)([kKmMgG]?)$", tokens[0])
        if not match:
            raise ValueError("invalid " + direction + " IOPS in " + path)
        value = float(match.group(1)) * {
            "": 1, "k": 1000, "m": 1000000, "g": 1000000000,
        }[match.group(2).lower()]
        if math.isnan(value) or math.isinf(value) or value <= 0:
            raise ValueError("nonpositive/nonfinite " + direction + " IOPS in " + path)
        result[direction] = value
    return result


def main(argv):
    if len(argv) == 2 and argv[0] == "directions":
        print("\n".join(directions(argv[1])))
    elif len(argv) == 3 and argv[0] == "validate":
        iops(argv[2], directions(argv[1]))
    elif len(argv) == 5 and argv[0] == "compare":
        required = directions(argv[1])
        direction = argv[4]
        if direction not in required:
            raise ValueError("unexpected direction " + direction)
        reference = iops(argv[2], required)[direction]
        current = iops(argv[3], required)[direction]
        delta = (current - reference) * 100 / reference
        trend = "higher" if delta > 0 else "lower" if delta < 0 else "unchanged"
        print("%s: reference=%g IOPS, current=%g IOPS; %g%% %s (informational)" %
              (direction, reference, current, abs(delta), trend))
    else:
        raise ValueError("usage: fio_metrics.py directions JOB | validate JOB RESULT | "
                         "compare JOB REFERENCE RESULT DIRECTION")


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (IOError, ValueError, OverflowError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        sys.exit(1)
