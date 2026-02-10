# Task: Replace Guest Test Files with Updated Versions

The previous test files had hardcoded device paths that don't match the actual QEMU layout. I have updated versions that use a `device_map.py` to auto-detect the correct devices.

## Step 1: Remove the old test files I created earlier

```bash
rm -rf eVSSIM/tests/guest/namespace_tests
rm -rf eVSSIM/tests/guest/sector_rw
rm -rf eVSSIM/tests/guest/object_rw
rm -f eVSSIM/tests/guest/run_dod_tests.sh
rm -f eVSSIM/tests/guest/device_map.py
```

## Step 2: Create the directory structure

```bash
mkdir -p eVSSIM/tests/guest/namespace_tests
mkdir -p eVSSIM/tests/guest/sector_rw
mkdir -p eVSSIM/tests/guest/object_rw
touch eVSSIM/tests/guest/namespace_tests/__init__.py
touch eVSSIM/tests/guest/sector_rw/__init__.py
touch eVSSIM/tests/guest/object_rw/__init__.py
```

## Step 3: Create the files

I will now paste the content of each file. Create them at exactly these paths:

1. `eVSSIM/tests/guest/device_map.py` — Device mapping (I'll paste content below)
2. `eVSSIM/tests/guest/namespace_tests/test_namespace_discovery.py` — Namespace discovery tests
3. `eVSSIM/tests/guest/sector_rw/test_sector_strategy.py` — Sector R/W tests
4. `eVSSIM/tests/guest/object_rw/test_object_strategy.py` — Object R/W tests
5. `eVSSIM/tests/guest/run_dod_tests.sh` — Runner script (make executable)

## Step 4: Make runner executable

```bash
chmod +x eVSSIM/tests/guest/run_dod_tests.sh
```

## Step 5: Verify final structure

```bash
find eVSSIM/tests/guest/ -type f | sort
```

Expected:
```
eVSSIM/tests/guest/device_map.py
eVSSIM/tests/guest/namespace_tests/__init__.py
eVSSIM/tests/guest/namespace_tests/test_namespace_discovery.py
eVSSIM/tests/guest/object_rw/__init__.py
eVSSIM/tests/guest/object_rw/test_object_strategy.py
eVSSIM/tests/guest/run_dod_tests.sh
eVSSIM/tests/guest/sector_rw/__init__.py
eVSSIM/tests/guest/sector_rw/test_sector_strategy.py
(plus existing files like objects_via_ioctl/, fio_tests/, etc.)
```

## Step 6: Also verify the builder.sh break is removed

```bash
grep -n "break" infra/builder/builder.sh | grep -i "namespace\|qemu dev"
```

If the break is still there (near "Currently the qemu dev support"), remove it.

---

## FILE CONTENTS TO CREATE:

I'm pasting all 5 files below. Please create each one at its specified path.
