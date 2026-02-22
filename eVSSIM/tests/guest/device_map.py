"""
NVMe Device Mapping for Guest Tests
====================================
Dynamically parsed from ssd.conf at import time.

The runtime environment (QEMU) is configured to instantiate one virtual
NVMe controller per [nvmeNN] section in the configuration file, with
multiple namespaces per controller. The guest sees /dev/nvmeXnY where
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


def _find_ssd_conf():
    """Locate ssd.conf: env override, then same directory as this file."""
    env_path = os.environ.get("SSD_CONF_PATH")
    if env_path and os.path.isfile(env_path):
        return env_path
    here = os.path.dirname(os.path.abspath(__file__))
    local = os.path.join(here, "ssd.conf")
    if os.path.isfile(local):
        return local
    return None


def _parse_ssd_conf(path):
    """Parse ssd.conf and return DEVICE_MAP dict."""
    device_map = {}
    ctrl_idx = -1
    device_name = None
    nsid = 0
    ns_props = {}
    # Track namespaces per controller for preference heuristics
    ctrl_ns_count = {}

    def _flush_ns():
        """Flush the current namespace into device_map."""
        if device_name is None or nsid == 0:
            return
        strategy = ns_props.get("STORAGE_STRATEGY", SECTOR)
        ns_name = "ns%02d" % nsid
        strategy_name = "Object" if strategy == OBJECT else "Sector"
        dev_path = "/dev/nvme%dn%d" % (ctrl_idx, nsid)
        entry = {
            "ctrl": ctrl_idx,
            "nsid": nsid,
            "device": device_name,
            "ns": ns_name,
            "strategy": strategy,
            "desc": "%s %s - %s" % (device_name, ns_name, strategy_name),
        }
        if strategy == OBJECT:
            entry["obj_capacity"] = ns_props.get("OBJECT_MAX_CAPACITY", 100)
        device_map[dev_path] = entry
        ctrl_ns_count.setdefault(ctrl_idx, 0)
        ctrl_ns_count[ctrl_idx] += 1

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Match [nvmeNN] section header
            m = re.match(r'^\[nvme(\d+)\]$', line)
            if m:
                _flush_ns()
                nsid = 0
                ns_props = {}
                ctrl_idx += 1
                device_name = "nvme%02d" % int(m.group(1))
                continue

            # Match [nsNN] subsection header
            m = re.match(r'^\[ns(\d+)\]$', line)
            if m:
                _flush_ns()
                nsid = int(m.group(1))
                ns_props = {}
                continue

            # Parse key-value pairs
            parts = line.split(None, 1)
            if len(parts) == 2:
                key, val = parts
                if key == "STORAGE_STRATEGY":
                    ns_props["STORAGE_STRATEGY"] = int(val)
                elif key == "OBJECT_MAX_CAPACITY":
                    ns_props["OBJECT_MAX_CAPACITY"] = int(val)
                elif key == "NAMESPACE_PAGE_NB":
                    ns_props["NAMESPACE_PAGE_NB"] = int(val)

        # Flush last namespace
        _flush_ns()

    return device_map, ctrl_ns_count


def _sort_key(dev):
    """Sort /dev/nvmeXnY by controller then namespace."""
    m = re.match(r'/dev/nvme(\d+)n(\d+)', dev)
    if m:
        return (int(m.group(1)), int(m.group(2)))
    return (999, 999)


# --- Build DEVICE_MAP at import time ---
_conf_path = _find_ssd_conf()
if _conf_path:
    DEVICE_MAP, _ctrl_ns_count = _parse_ssd_conf(_conf_path)
else:
    DEVICE_MAP = {}
    _ctrl_ns_count = {}

EXPECTED_DEVICE_COUNT = len(DEVICE_MAP)
EXPECTED_CONTROLLER_COUNT = len(set(info["ctrl"]
                                    for info in DEVICE_MAP.values()))
EXPECTED_SECTOR_COUNT = sum(1 for info in DEVICE_MAP.values()
                            if info["strategy"] == SECTOR)
EXPECTED_OBJECT_COUNT = sum(1 for info in DEVICE_MAP.values()
                            if info["strategy"] == OBJECT)


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
    devs = get_sector_devices()
    # Prefer a device on a single-namespace controller (full drive size)
    for d in devs:
        info = DEVICE_MAP[d]
        if _ctrl_ns_count.get(info["ctrl"], 0) == 1 and os.path.exists(d):
            return d
    for d in devs:
        if os.path.exists(d):
            return d
    return devs[0] if devs else None


def get_first_object_device():
    """Return first available object device (prefers pure-object controller)."""
    devs = get_object_devices()
    # Prefer a device on a controller where ALL namespaces are object
    for d in devs:
        info = DEVICE_MAP[d]
        ctrl = info["ctrl"]
        all_obj = all(v["strategy"] == OBJECT
                      for v in DEVICE_MAP.values() if v["ctrl"] == ctrl)
        if all_obj and os.path.exists(d):
            return d
    for d in devs:
        if os.path.exists(d):
            return d
    return devs[0] if devs else None


def get_largest_sector_device():
    """Return sector device on single-namespace controller (full drive size)."""
    devs = get_sector_devices()
    for d in devs:
        info = DEVICE_MAP[d]
        if _ctrl_ns_count.get(info["ctrl"], 0) == 1 and os.path.exists(d):
            return d
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


if __name__ == "__main__":
    if _conf_path:
        print("Parsed from: %s" % _conf_path)
    else:
        print("WARNING: ssd.conf not found")
    print("")
    describe_all()
    print("")
    print("Total devices:      %d" % EXPECTED_DEVICE_COUNT)
    print("Controllers:        %d" % EXPECTED_CONTROLLER_COUNT)
    print("Sector devices:     %d" % EXPECTED_SECTOR_COUNT)
    print("Object devices:     %d" % EXPECTED_OBJECT_COUNT)
    print("")
    print("Sector devices: %s" % get_sector_devices())
    print("Object devices: %s" % get_object_devices())
    print("First sector:   %s" % get_first_sector_device())
    print("First object:   %s" % get_first_object_device())
