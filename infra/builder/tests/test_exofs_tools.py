"""S4 tool publication and guest-wrapper contracts; no real build or guest."""

import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


BUILDER = Path(__file__).resolve().parents[1]


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


class ExofsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="exofs-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.bin = self.root / "bin"
        self.scripts = self.root / "simulator/infra/builder"
        for name in ("compile-exofs-tools.sh", "compile-osd-exofs.sh", "docker-test-exofs.sh"):
            write(self.scripts / name, (BUILDER / name).read_text(), True)
        self.profiles = json.loads((BUILDER / "versions/configs.json").read_text())
        write(self.scripts / "versions/configs.json", json.dumps(self.profiles))
        write(self.root / "simulator/eVSSIM/osc-osd/source", "committed osc source\n")
        write(self.root / "simulator/eVSSIM/scripts/exofs/run_osd_emulator_and_mount_exofs.sh", "mount test\n")
        write(self.root / "kernel/source", "committed kernel source\n")
        write(self.root / "open-osd/source", "committed osd source\n")
        for repo in ("simulator", "kernel", "open-osd"):
            self.git(repo, "init", "-q")
            self.git(repo, "add", "--", *(str(p.relative_to(self.root / repo))
                     for p in (self.root / repo).rglob("*") if p.is_file() and ".git" not in p.parts))
            self.git(repo, "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                     "commit", "-qm", "fixture")
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        EVSSIM_ENVIRONMENT="yes", EVSSIM_ROOT_PATH=str(self.root),
                        EVSSIM_SIMULATOR_FOLDER="simulator", EVSSIM_KERNEL_FOLDER="kernel",
                        EVSSIM_DIST_FOLDER="dist", EVSSIM_TOOLS_FOLDER="dist/tools",
                        EVSSIM_LOGS_FOLDER="logs", EVSSIM_GUEST_HOME_PATH="/home/esd",
                        EVSSIM_DOCKER_IMAGE_NAME="evssim", EVSSIM_GUEST_TESTS_COMPILE_CONTAINER="ubuntu-14.04",
                        EVSSIM_VERSIONS_CONFIGURATION_ID="4", EVSSIM_BUILDER_FOLDER="simulator/infra/builder",
                        EVSSIM_VERSIONS_FOLDER="simulator/infra/builder/versions", TEST_ROOT=str(self.root))
        # Run the real container shell against its exported sources, substituting
        # only the container mount and compiler/rsync boundaries.
        write(self.bin / "docker", '''#!/usr/bin/env python3
import os, subprocess, sys
from pathlib import Path
args = sys.argv[1:]
if args[:2] == ['image', 'inspect']:
    print(os.environ.get('TEST_IMAGE', 'sha256:fixture'))
else:
    mount = args[args.index('--volume') + 1].rsplit(':', 1)[0]
    pos = args.index('-c')
    script = args[pos + 1].replace('/work', mount)
    sys.exit(subprocess.run(['bash', '-euo', 'pipefail', '-c', script, *args[pos + 2:]]).returncode)
''', True)
        write(self.bin / "make", '''#!/usr/bin/env python3
import os, sys
from pathlib import Path
p = Path.cwd()
stage = p.name + ('-clean' if 'clean' in sys.argv else '-build')
with (Path(os.environ['TEST_ROOT']) / 'make.log').open('a') as out:
    out.write(stage + '\\n')
if os.environ.get('TEST_FAIL') == stage:
    sys.exit(7)
if 'clean' not in sys.argv:
    names = ['up', 'tgt/usr/otgtd', 'tgt/usr/tgtadm'] if p.name == 'osc-osd' else ['usr/mkfs.exofs', 'lib/libosd.so']
    for name in names:
        if name == os.environ.get('TEST_OMIT'):
            continue
        f = p / name
        f.parent.mkdir(parents=True, exist_ok=True)
        f.write_text('compiled fixture\\n')
        f.chmod(0o755)
''', True)
        write(self.bin / "rsync", '''#!/usr/bin/env python3
import shutil, sys
shutil.copytree(sys.argv[-2], sys.argv[-1], dirs_exist_ok=True)
''', True)
        write(self.scripts / "builder.sh", '''set -euo pipefail
evssim_validate_version_arguments() { :; }
event() { echo "$1" >> "$TEST_ROOT/guest.log"; return "$2"; }
evssim_qemu_fresh_image() { event prepare "${TEST_PREPARE_RC:-0}"; }
evssim_qemu_detached() { echo "launch:$EVSSIM_RUNTIME_STORAGE_STRATEGY:$EVSSIM_QEMU_SIMULATOR_ENABLED:$1" >> "$TEST_ROOT/guest.log"; }
evssim_guest() { event test "${TEST_TEST_RC:-0}"; }
evssim_copy_from_guest() { event collection "${TEST_COLLECTION_RC:-0}"; }
evssim_qemu_flush_disk() { event flush "${TEST_FLUSH_RC:-0}"; }
evssim_qemu_stop() { event stop "${TEST_STOP_RC:-0}"; }
''')
        self.tools = self.root / "dist/tools"
        self.record_path = self.root / "dist/exofs-tools.json"

    def git(self, repo, *args):
        return subprocess.check_output(['git', '-C', str(self.root / repo), *args], text=True).strip()

    def run_script(self, name, version="ubuntu-14.04"):
        return subprocess.run(['bash', str(self.scripts / name), version], cwd=self.scripts,
                              env=self.env, text=True, capture_output=True, timeout=30)

    def build(self):
        result = self.run_script('compile-exofs-tools.sh')
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        return json.loads(self.record_path.read_text())

    def guest_events(self):
        path = self.root / 'guest.log'
        return path.read_text().splitlines() if path.exists() else []

    def test_build_and_verify_both_profiles_preserves_core_and_records_inputs(self):
        core = self.tools / 'usr/local/sbin/nvme'
        write(core, 'core payload')
        for profile in (2, 4):
            with self.subTest(profile=profile):
                self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = str(profile)
                record = self.build()
                self.assertEqual(next(p for p in self.profiles['configs'] if p['id'] == profile), record['profile'])
                self.assertEqual('sha256:fixture', record['container']['image_id'])
                self.assertEqual(hashlib.sha256((self.scripts / 'compile-exofs-tools.sh').read_bytes()).hexdigest(),
                                 record['recipe_sha256'])
                for repo in ('simulator', 'kernel', 'open-osd'):
                    self.assertEqual(self.git(repo, 'rev-parse', 'HEAD'), record['sources'][repo])
                self.assertIn('usr/local/sbin/mkfs.exofs', record['files'])
                self.assertIn('usr/local/lib/libosd.so', record['files'])
                self.assertIn('home/esd/osc-osd/up', record['files'])
                self.assertIn('home/esd/exofs/run_osd_emulator_and_mount_exofs.sh', record['files'])
                self.assertEqual('core payload', core.read_text())
                for name, info in record['files'].items():
                    self.assertEqual(hashlib.sha256((self.tools / name).read_bytes()).hexdigest(), info['sha256'])
                self.assertEqual(0, self.run_script('docker-test-exofs.sh', 'ubuntu-26.04').returncode)
        self.assertEqual(['osc-osd-clean', 'osc-osd-build', 'open-osd-clean', 'open-osd-build'] * 2,
                         (self.root / 'make.log').read_text().splitlines())
        self.assertFalse(any((self.root / 'logs').glob('*/workspace')))
        self.assertFalse((self.root / 'simulator/eVSSIM/osc-osd/up').exists())

    def test_build_stage_failure_never_publishes_and_retains_log(self):
        for stage in ('osc-osd-clean', 'osc-osd-build', 'open-osd-clean', 'open-osd-build'):
            with self.subTest(stage=stage):
                self.env['TEST_FAIL'] = stage
                result = self.run_script('compile-exofs-tools.sh')
                self.assertNotEqual(0, result.returncode)
                self.assertFalse(self.record_path.exists())
                self.assertFalse(any((self.root / 'logs').glob('*/workspace')))
        self.assertEqual(4, len(list((self.root / 'logs').glob('*/build.log'))))

    def test_missing_build_output_prevents_publication(self):
        for missing in ('up', 'tgt/usr/otgtd', 'tgt/usr/tgtadm', 'usr/mkfs.exofs', 'lib/libosd.so'):
            with self.subTest(missing=missing):
                self.env['TEST_OMIT'] = missing
                self.assertNotEqual(0, self.run_script('compile-exofs-tools.sh').returncode)
                self.assertFalse(self.record_path.exists())

    def test_dirty_sources_or_missing_repository_prevent_build(self):
        for repo, relative in (('simulator', 'eVSSIM/osc-osd/source'), ('kernel', 'source'), ('open-osd', 'source')):
            path = self.root / repo / relative
            original = path.read_text()
            path.write_text('dirty')
            with self.subTest(repo=repo):
                self.assertNotEqual(0, self.run_script('compile-exofs-tools.sh').returncode)
                self.assertFalse((self.root / 'make.log').exists())
            path.write_text(original)
        shutil.rmtree(self.root / 'kernel')
        self.assertNotEqual(0, self.run_script('compile-exofs-tools.sh').returncode)
        self.assertFalse((self.root / 'make.log').exists())

    def test_wrong_container_prevents_build(self):
        self.assertNotEqual(0, self.run_script('compile-exofs-tools.sh', 'ubuntu-26.04').returncode)
        self.assertFalse((self.root / 'make.log').exists())

    def test_missing_and_modified_outputs_prevent_guest_preparation(self):
        record = self.build()
        for name in record['files']:
            path = self.tools / name
            original = path.read_bytes()
            mode = path.stat().st_mode & 0o777
            for content in (None, b'corrupt'):
                with self.subTest(name=name, content=content):
                    if content is None:
                        path.unlink()
                    else:
                        path.write_bytes(content)
                    self.assertNotEqual(0, self.run_script('docker-test-exofs.sh').returncode)
                    self.assertEqual([], self.guest_events())
                    path.write_bytes(original)
                    path.chmod(mode)
        self.assertEqual(0, self.run_script('docker-test-exofs.sh').returncode)

    def test_mismatched_record_prevents_guest_preparation(self):
        record = self.build()
        for field in ('profile', 'container', 'sources', 'recipe_sha256', 'files'):
            broken = copy.deepcopy(record)
            broken[field] = {} if isinstance(broken[field], dict) else 'changed'
            self.record_path.write_text(json.dumps(broken))
            with self.subTest(field=field):
                self.assertNotEqual(0, self.run_script('docker-test-exofs.sh').returncode)
                self.assertEqual([], self.guest_events())
        self.record_path.unlink()
        self.assertNotEqual(0, self.run_script('docker-test-exofs.sh').returncode)
        self.assertEqual([], self.guest_events())

    def test_guest_results_preserve_primary_error_and_attempt_all_cleanup(self):
        self.build()
        for statuses in ((0, 0, 0, 0), (7, 5, 4, 3), (0, 5, 4, 3), (0, 0, 4, 3), (0, 0, 0, 3)):
            with self.subTest(statuses=statuses):
                (self.root / 'guest.log').unlink(missing_ok=True)
                for name, code in zip(('TEST', 'COLLECTION', 'FLUSH', 'STOP'), statuses):
                    self.env['TEST_' + name + '_RC'] = str(code)
                result = self.run_script('docker-test-exofs.sh', 'ubuntu-26.04')
                self.assertEqual(next((code for code in statuses if code), 0), result.returncode)
                self.assertEqual(['prepare', 'launch:2:yes:ubuntu-26.04', 'test', 'collection', 'flush', 'stop'],
                                 self.guest_events())
                self.assertIn('test=%s collection=%s flush=%s stop=%s' % statuses, result.stdout)

    def test_failed_preparation_never_launches(self):
        self.build()
        self.env['TEST_PREPARE_RC'] = '9'
        self.assertEqual(9, self.run_script('docker-test-exofs.sh').returncode)
        self.assertEqual(['prepare'], self.guest_events())

    def test_upstream_entry_point_delegates_once_and_preserves_exit_status(self):
        write(self.scripts / 'compile-exofs-tools.sh', '#!/bin/bash\necho "$@" >> "$TEST_ROOT/delegated.log"\nexit 7\n', True)
        self.assertEqual(7, self.run_script('compile-osd-exofs.sh').returncode)
        self.assertEqual(['ubuntu-14.04'], (self.root / 'delegated.log').read_text().splitlines())
        self.assertFalse((self.root / 'make.log').exists())
