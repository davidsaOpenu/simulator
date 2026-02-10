"""
NVMe Device Mapping for Guest Tests
====================================
Based on ACTUAL guest VM output (nvme list).

The builder creates one NVMe controller per ssd.conf device section,
with multiple namespaces per controller. Guest sees /dev/nvmeXnY where
X = controller index (0-based), Y = namespace ID (1-based).

Usage:
    from device_map import get_first_sector_device, get_first_object_device
    from device_map import get_sector_devices, get_object_devices
"""

import os
import re

# Strategy constants
SECTOR = 1
OBJECT = 2

# Actual device map based on observed guest topology.
DEVICE_MAP = {
    # Controller 0 (serial=1) = nvme01: 1 namespace (Object)
    "/dev/nvme0n1": {"ctrl": 0, "nsid": 1, "device": "nvme01", "ns": "ns01",
                     "strategy": OBJECT, "desc": "nvme01 ns01 - Object",
                     "obj_capacity": 100},

    # Controller 1 (serial=2) = nvme02: 1 namespace
    "/dev/nvme1n1": {"ctrl": 1, "nsid": 1, "device": "nvme02", "ns": "ns01",
                     "strategy": SECTOR, "desc": "nvme02 ns01 - Sector"},

    # Controller 2 (serial=3) = nvme03: 2 namespaces
    "/dev/nvme2n1": {"ctrl": 2, "nsid": 1, "device": "nvme03", "ns": "ns01",
                     "strategy": SECTOR, "desc": "nvme03 ns01 - Sector"},
    "/dev/nvme2n2": {"ctrl": 2, "nsid": 2, "device": "nvme03", "ns": "ns02",
                     "strategy": OBJECT, "desc": "nvme03 ns02 - Object"},

    # Controller 3 (serial=4) = nvme04: 2 namespaces
    "/dev/nvme3n1": {"ctrl": 3, "nsid": 1, "device": "nvme04", "ns": "ns01",
                     "strategy": OBJECT, "desc": "nvme04 ns01 - Object"},
    "/dev/nvme3n2": {"ctrl": 3, "nsid": 2, "device": "nvme04", "ns": "ns02",
                     "strategy": SECTOR, "desc": "nvme04 ns02 - Sector"},

    # Controller 4 (serial=5) = nvme05: 4 namespaces (all Sector)
    "/dev/nvme4n1": {"ctrl": 4, "nsid": 1, "device": "nvme05", "ns": "ns01",
                     "strategy": SECTOR, "desc": "nvme05 ns01 - Sector"},
    "/dev/nvme4n2": {"ctrl": 4, "nsid": 2, "device": "nvme05", "ns": "ns02",
                     "strategy": SECTOR, "desc": "nvme05 ns02 - Sector"},
    "/dev/nvme4n3": {"ctrl": 4, "nsid": 3, "device": "nvme05", "ns": "ns03",
                     "strategy": SECTOR, "desc": "nvme05 ns03 - Sector"},
    "/dev/nvme4n4": {"ctrl": 4, "nsid": 4, "device": "nvme05", "ns": "ns04",
                     "strategy": SECTOR, "desc": "nvme05 ns04 - Sector"},

    # Controller 5 (serial=6) = nvme06: 4 namespaces (all Object)
    "/dev/nvme5n1": {"ctrl": 5, "nsid": 1, "device": "nvme06", "ns": "ns01",
                     "strategy": OBJECT, "desc": "nvme06 ns01 - Object",
                     "obj_capacity": 100},
    "/dev/nvme5n2": {"ctrl": 5, "nsid": 2, "device": "nvme06", "ns": "ns02",
                     "strategy": OBJECT, "desc": "nvme06 ns02 - Object",
                     "obj_capacity": 100},
    "/dev/nvme5n3": {"ctrl": 5, "nsid": 3, "device": "nvme06", "ns": "ns03",
                     "strategy": OBJECT, "desc": "nvme06 ns03 - Object",
                     "obj_capacity": 100},
    "/dev/nvme5n4": {"ctrl": 5, "nsid": 4, "device": "nvme06", "ns": "ns04",
                     "strategy": OBJECT, "desc": "nvme06 ns04 - Object",
                     "obj_capacity": 100},
}


def _sort_key(dev):
    """Sort /dev/nvmeXnY by controller then namespace."""
    m = re.match(r'/dev/nvme(\d+)n(\d+)', dev)
    if m:
        return (int(m.group(1)), int(m.group(2)))
    return (999, 999)


def get_all_devices():
    """Return sorted list of all expected guest device paths."""
    return sorted(DEVICE_MAP.keys(), key=_sort_key)


def get_sector_devices():
    """Return sorted list of sector-strategy device paths."""
    return sorted([d for d, info in DEVICE_MAP.items()
                   if info["strategy"] == SECTOR], key=_sort_key)


def get_object_devices():
    """Return sorted list of object-strategy device paths."""
    return sorted([d for d, info in DEVICE_MAP.items()
                   if info["strategy"] == OBJECT], key=_sort_key)


def get_first_sector_device():
    """Return first available sector device (prefers single-ns controller)."""
    # Prefer nvme1n1 (nvme02, single namespace = cleanest, full size)
    if os.path.exists("/dev/nvme1n1"):
        return "/dev/nvme1n1"
    devs = get_sector_devices()
    for d in devs:
        if os.path.exists(d):
            return d
    return None


def get_first_object_device():
    """Return first available object device (prefers pure-object controller)."""
    # Prefer nvme5n1 (nvme06 ns01, pure object controller)
    if os.path.exists("/dev/nvme5n1"):
        return "/dev/nvme5n1"
    devs = get_object_devices()
    for d in devs:
        if os.path.exists(d):
            return d
    return None


def get_largest_sector_device():
    """Return sector device on single-namespace controller (full drive size)."""
    if os.path.exists("/dev/nvme1n1"):
        return "/dev/nvme1n1"
    return get_first_sector_device()


def get_object_capacity(guest_dev):
    """Return OBJECT_MAX_CAPACITY for a given object device."""
    info = DEVICE_MAP.get(guest_dev)
    if info:
        return info.get("obj_capacity", 100)
    return 100


def describe_all():
    """Print human-readable description of all devices."""
    print("Guest Device       Ctrl  NSID  Source          Strategy  Exists")
    print("-" * 70)
    for dev in get_all_devices():
        info = DEVICE_MAP[dev]
        strategy_name = "Sector" if info["strategy"] == SECTOR else "Object"
        source = "%s %s" % (info["device"], info["ns"])
        exists = "YES" if os.path.exists(dev) else "NO"
        print("%-18s %-5d %-5d %-15s %-9s %s" % (
            dev, info["ctrl"], info["nsid"], source, strategy_name, exists))


EXPECTED_DEVICE_COUNT = len(DEVICE_MAP)  # 14
EXPECTED_CONTROLLER_COUNT = 6
EXPECTED_SECTOR_COUNT = 7
EXPECTED_OBJECT_COUNT = 7


if __name__ == "__main__":
    describe_all()
    print("")
    print("Sector devices: %s" % get_sector_devices())
    print("Object devices: %s" % get_object_devices())
    print("First sector:   %s" % get_first_sector_device())
    print("First object:   %s" % get_first_object_device())
