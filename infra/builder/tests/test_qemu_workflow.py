"""Exercise workflow status/evidence with controlled build and runtime boundaries."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import test_dependency_handler as dependency_tests

BUILDER = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("workflow", BUILDER / "qemu-workflow.py")
workflow = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(workflow)

STUB = r'''#!/usr/bin/env python3
import hashlib, importlib.util, json, os, pathlib, shutil, subprocess, sys
name = pathlib.Path(sys.argv[0]).name
env = os.environ
with open(env['S2B_TEST_CALL_LOG'], 'a') as stream:
    stream.write('|'.join([name, env['EVSSIM_VERSIONS_CONFIGURATION_ID'], env['EVSSIM_QEMU_BRANCH'],
        env['EVSSIM_QEMU_COMPILE_CONTAINER'], env['EVSSIM_GUEST_TESTS_COMPILE_CONTAINER'],
        ' '.join(sys.argv[1:]), env.get('EVSSIM_QEMU_ARTIFACT_SELECTION', '')]) + '\n')
print('controlled output from ' + name, flush=True)
if env.get('S7_TEST_FAIL') == name:
    sys.exit(7)
if name == 'elk_cleanup.sh':
    sys.exit(int(env.get('S7_TEST_CLEANUP_STATUS', '0')))
if name in ('docker-run-sanity.sh', 'docker-test-guest.sh', 'docker-test-exofs.sh'):
    folder = pathlib.Path(env['EVSSIM_WORKFLOW_STAGE_DIR'])
    helper = pathlib.Path(env['EVSSIM_ROOT_PATH']) / 'simulator/infra/builder/qemu-workflow.py'
    spec = importlib.util.spec_from_file_location('artifact', helper.with_name('qemu-artifact.py'))
    artifact = importlib.util.module_from_spec(spec); spec.loader.exec_module(artifact)
    selection = json.loads(pathlib.Path(env['EVSSIM_QEMU_ARTIFACT_SELECTION']).read_text())
    profiles = json.loads((helper.parent / 'versions/configs.json').read_text())
    runtime = artifact.runtime_selection(selection, profiles, env)
    for number in range(1, 5 if name == 'docker-test-guest.sh' else 2):
        config = folder / 'input.ssd.conf'; config.write_text('[nvme01]\n[ns01]\n')
        subprocess.run([sys.executable, str(helper), '--directory', str(folder), 'launch',
            '--runtime', json.dumps(runtime), '--container', 'controlled-container-' + str(number),
            '--devices', '1', '--strategy', '2' if name == 'docker-test-exofs.sh' or number == 4 else '1',
            '--simulator', 'no' if name == 'docker-test-guest.sh' and number == 2 else 'yes',
            '--config', str(config)], check=True)
        if name == 'docker-test-guest.sh':
            output = folder / 'guest' / str(number); output.mkdir(parents=True)
            (output / 'guest_tests_results.xml').write_text('<testsuite tests="1" failures="0" errors="0"/>')
            dist = pathlib.Path(env['EVSSIM_ROOT_PATH']) / 'dist'
            (output / 'guest-tools.sha256').write_text(''.join(hashlib.sha256((dist / n).read_bytes()).hexdigest()
                + '  ' + n + '\n' for n in ('nvme', 'tnvme', 'dnvme.ko')))
            (output / 'guest-kernel.txt').write_text(env['EVSSIM_KERNEL_DIST'])
    if name == 'docker-test-guest.sh':
        fio = folder / 'guest/2/fio'
        source = pathlib.Path(env['EVSSIM_ROOT_PATH']) / 'simulator/eVSSIM/tests/guest/fio_tests/fio_jobs'
        shutil.copytree(source, fio / 'jobs')
        (fio / 'current').mkdir(); (fio / 'references').mkdir()
        for job, directions in [('test_write_short', ['write']), ('test_rw_short', ['write', 'read']), ('test_read_short', ['read'])]:
            text = ''.join(d + ': IOPS=100\n' for d in directions)
            (fio / 'references' / (job + '_ref_res')).write_text(text)
            for direction in directions:
                (fio / 'current' / (job + '_' + direction + '_res.fixture')).write_text(text)
    if name == 'docker-test-exofs.sh':
        output = folder / 'guest-output'; output.mkdir()
        (output / 'trace.log').write_text('controlled exofs result\n')
'''

ARTIFACT_STUB = r'''import importlib.util, json, os, pathlib, subprocess, sys
spec = importlib.util.spec_from_file_location('implementation', pathlib.Path(__file__).with_name('artifact-implementation.py'))
implementation = importlib.util.module_from_spec(spec); spec.loader.exec_module(implementation)
inspect_image = implementation.inspect_image
runtime_selection = implementation.runtime_selection
if __name__ == '__main__':
    if sys.argv[1] == '--profile-from-inputs':
        with open(sys.argv[2]) as stream:
            print(json.dumps(json.load(stream)['inputs']['profile']))
        sys.exit(0)
    env = os.environ
    with open(env['S2B_TEST_CALL_LOG'], 'a') as stream:
        stream.write('|'.join(['qemu-artifact.py', env['EVSSIM_VERSIONS_CONFIGURATION_ID'],
            env['EVSSIM_QEMU_BRANCH'], env['EVSSIM_QEMU_COMPILE_CONTAINER'],
            env['EVSSIM_GUEST_TESTS_COMPILE_CONTAINER'], ' '.join(sys.argv[1:]), '']) + '\n')
    if env.get('S7_TEST_FAIL') == 'qemu-artifact.py':
        print('controlled artifact failure', file=sys.stderr); sys.exit(7)
    root = pathlib.Path(env['EVSSIM_ROOT_PATH'])
    profiles = json.loads((pathlib.Path(__file__).parent / 'versions/configs.json').read_text())
    profile = next(p for p in profiles['configs'] if p['id'] == int(env['EVSSIM_VERSIONS_CONFIGURATION_ID']))
    policy = json.loads((pathlib.Path(__file__).parent / 'versions/qemu-build-policy.json').read_text())
    recipe = policy['builds'][profile['qemu']['compileContainer']]
    version = '11.0.0' if profile['qemu']['branch'] == '11.0' else '2.12.0'
    mode = '--resolved-inputs' if '--resolved-inputs' in sys.argv else '--saved-inputs'
    original = json.loads(pathlib.Path(sys.argv[sys.argv.index(mode) + 1]).read_text())
    report = original['dependencies'] if mode == '--resolved-inputs' else original.get('dependency_report', [])
    inputs = dict(schema_version=1, profile=profile, sources={}, recipes={}, dependencies=implementation.artifact_dependencies(report),
        container=dict(image_id='sha256:' + '1' * 64, os='linux', architecture='amd64', variant=''))
    for name in ('qemu', 'simulator'):
        inputs['sources'][name] = dict(revision=subprocess.check_output(
            ['git', '-C', str(root / name), 'rev-parse', 'HEAD'], text=True).strip(),
            target_branch=profile['qemu']['branch'] if name == 'qemu' else 'master')
    inputs['sources']['qemu']['version'] = version
    for name in ('target', 'configure_args', 'build_environment'): inputs[name] = recipe[name]
    identity = dict(inputs=inputs, key=implementation.input_key(inputs), dependency_report=report)
    bundle = pathlib.Path(sys.argv[sys.argv.index('--build-dir') + 1]) / 'fixture-bundle'
    files = ['bin/qemu-system-x86_64'] + ['share/qemu/' + x for x in implementation.REQUIRED_FIRMWARE]
    files += [implementation.VSSIM_WWW_DIR + '/' + x for x in implementation.REQUIRED_MONITOR_ASSETS]
    for name in files:
        path = bundle / name; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('controlled artifact payload\n'); path.chmod(0o755)
    manifest = dict(schema_version=1, key=identity['key'], inputs=inputs, version=version,
        executable='bin/qemu-system-x86_64', data_dir='share/qemu', vssim_www_dir=implementation.VSSIM_WWW_DIR,
        files=implementation.file_inventory(bundle), build='full-clean-passed')
    (bundle / 'manifest.json').write_text(json.dumps(manifest))
    selection = implementation.artifact_selection(identity, bundle, bundle, env.get('S7_TEST_CACHE', 'miss'))
    if env.get('S7_TEST_BAD_ARTIFACT'): selection['version'] = '0.0.0'
    print(json.dumps(selection))
'''


def workflow_fixture(root):
    target = root / "workflow/simulator/infra/builder"
    target.mkdir(parents=True, exist_ok=True)
    for name in ("run-ci.sh", "env.sh", "load_config.sh", "compile-qemu.sh", "qemu-inputs.py", "qemu-workflow.py"):
        shutil.copy2(BUILDER / name, target / name)
    shutil.copy2(BUILDER / "qemu-artifact.py", target / "artifact-implementation.py")
    (target / "qemu-artifact.py").write_text(ARTIFACT_STUB)
    (target / "versions").mkdir(exist_ok=True)
    for name in ("configs.json", "qemu-build-policy.json"):
        shutil.copy2(BUILDER / "versions" / name, target / "versions" / name)
    fio = target.parents[1] / 'eVSSIM/tests/guest/fio_tests'
    fio.mkdir(parents=True, exist_ok=True)
    original_fio = BUILDER.parents[1] / 'eVSSIM/tests/guest/fio_tests'
    shutil.copy2(original_fio / 'fio_metrics.py', fio / 'fio_metrics.py')
    shutil.copytree(original_fio / 'fio_jobs', fio / 'fio_jobs', dirs_exist_ok=True)
    workspace = target.parents[2]
    (workspace / 'dist').mkdir(exist_ok=True)
    for name in ('nvme', 'tnvme', 'dnvme.ko'):
        (workspace / 'dist' / name).write_text('controlled guest tool ' + name)
    for name in workflow.REPOSITORIES:
        repo = workspace / name
        if not (repo / ".git").exists():
            repo.mkdir(parents=True, exist_ok=True)
            for args in (("init", "-q", "--initial-branch=master"), ("config", "user.name", "Workflow test"),
                         ("config", "user.email", "workflow@example.invalid"), ("config", "commit.gpgsign", "false")):
                dependency_tests.git(repo, *args)
            dependency_tests.write(repo, "source", name + " fixture\n")
            dependency_tests.commit(repo, "source")
    elk = target.parent / "ELK"; elk.mkdir(exist_ok=True)
    tools = root / "bin"; tools.mkdir(exist_ok=True)
    scripts = ("check_tools.sh", "build-docker-image.sh", "prepare-qemu-sources.sh", "build-qemu-image.sh", "compile-kernel.sh",
               "compile-exofs-tools.sh", "compile-host-tests.sh", "compile-guest-tests.sh", "docker-run-sanity.sh",
               "docker-test-host.sh", "docker-test-host-elk.sh", "docker-test-guest.sh", "docker-test-exofs.sh")
    for path in ([target / x for x in scripts] + [tools / "tox"] +
                 [elk / x for x in ("install_and_start_elk.sh", "elk_cleanup.sh")]):
        path.write_text(STUB); path.chmod(0o755)
    # Match the real checker's shell body without a shebang. A direct exec
    # from the recorder must not accidentally pass through a Python stub.
    checker = STUB.replace('name = pathlib.Path(sys.argv[0]).name', "name = 'check_tools.sh'")
    (target / "check_tools.sh").write_text("python3 - <<'PY'\n" + checker + "PY\n")
    docker = tools / "docker"
    docker.write_text('''#!/usr/bin/env python3
import json, os, sys
if sys.argv[-1] == '--version':
    print('QEMU emulator version ' + ('11.0.0' if os.environ['EVSSIM_QEMU_BRANCH'] == '11.0' else '2.12.0'))
else:
    print(json.dumps(dict(Id='sha256:' + '1' * 64, Os='linux', Architecture='amd64', Variant='')))
''')
    docker.chmod(0o755)
    log = root / "calls.log"; log.write_text("")
    env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"], S2B_TEST_CALL_LOG=str(log))
    for name in ("EVSSIM_VERSIONS_CONFIGURATION_ID", "EVSSIM_QEMU_INPUT_RECORD", "EVSSIM_DEPENDENCY_RECORD",
                 "EVSSIM_QEMU_ARTIFACT_SELECTION", "EVSSIM_WORKFLOW_STAGE_DIR", "GERRIT_CHANGE_NUMBER",
                 "S7_TEST_CACHE", "S7_TEST_FAIL", "S7_TEST_BAD_ARTIFACT", "S7_TEST_CLEANUP_STATUS"):
        env.pop(name, None)
    return target, env, log


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-workflow-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.target, self.env, self.log = workflow_fixture(self.root)
        self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = '2'

    def run_workflow(self, timeout=40):
        command = ['bash', str(self.target / 'run-ci.sh')]
        with subprocess.Popen(command, cwd=self.root, env=self.env, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, start_new_session=True) as child:
            try:
                stdout, stderr = child.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                # Killing only Bash leaves the recorder/command writing into
                # the next case's log. This session belongs only to the fixture.
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.communicate()
                raise
        process = subprocess.CompletedProcess(command, child.returncode, stdout, stderr)
        paths = list((self.root / 'workflow/logs/qemu11-validation').glob('run.*'))
        return process, max(paths, key=lambda p: p.stat().st_mtime_ns)

    def test_timeout_does_not_leave_fixture_commands_running(self):
        pidfile = self.root / 'child.pid'
        self.env['FIXTURE_CHILD_PID'] = str(pidfile)
        (self.target / 'run-ci.sh').write_text('sleep 30 &\necho $! > "$FIXTURE_CHILD_PID"\nwait\n')
        with self.assertRaises(subprocess.TimeoutExpired):
            self.run_workflow(timeout=2)
        self.assertTrue(pidfile.exists(), 'fixture must reach the blocking child')
        state = Path('/proc') / pidfile.read_text().strip() / 'stat'
        try:
            status = state.read_text().split()[2]
        except FileNotFoundError:
            status = None
        self.assertIn(status, (None, 'Z'), 'timed-out fixture command is still running')

    def finish(self, directory):
        return subprocess.run(['bash', '-c', 'source ./env.sh >/dev/null; exec python3 ./qemu-workflow.py --directory "$1" finish 0',
            'bash', str(directory)], cwd=self.target, env=self.env, capture_output=True, text=True, timeout=15)

    def test_real_tool_checker_without_shebang_runs_through_recorded_bash(self):
        shutil.copy2(BUILDER / 'check_tools.sh', self.target / 'check_tools.sh')
        for name in ('docker', 'docker-compose', 'ansible', 'tox'):
            path = self.root / 'bin' / name
            path.write_text('#!/bin/sh\necho fixture-version\n')
            path.chmod(0o755)
        directory = self.root / 'checker-evidence'
        command = ['bash', './check_tools.sh']
        process = subprocess.run(['python3', str(self.target / 'qemu-workflow.py'),
            '--directory', str(directory), 'stage', 'check-tools', '--'] + command,
            cwd=self.target, env=self.env, capture_output=True, text=True, timeout=15)
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        record = json.loads((directory / 'stages/check-tools/result.json').read_text())
        self.assertEqual(command, record['argv'])
        self.assertEqual(0, record['child_exit_status'])
        self.assertEqual(4, process.stdout.count('Success.'))

    def test_every_required_child_failure_preserves_seven_and_stops_later_stages(self):
        commands = ['check_tools.sh', 'tox', 'build-docker-image.sh', 'prepare-qemu-sources.sh', 'build-qemu-image.sh', 'compile-kernel.sh',
                    'qemu-artifact.py', 'compile-host-tests.sh', 'compile-guest-tests.sh', 'compile-exofs-tools.sh',
                    'docker-run-sanity.sh', 'install_and_start_elk.sh', 'docker-test-host.sh',
                    'docker-test-host-elk.sh', 'docker-test-guest.sh', 'docker-test-exofs.sh']
        for failed in commands:
            with self.subTest(command=failed):
                self.env['S7_TEST_FAIL'] = failed
                self.log.write_text('')
                process, directory = self.run_workflow()
                self.assertEqual(7, process.returncode, process.stdout + process.stderr)
                self.assertEqual(7, json.loads((directory / 'result.json').read_text())['exit_status'])
                calls = [line.split('|')[0] for line in self.log.read_text().splitlines()]
                self.assertEqual(commands[:commands.index(failed) + 1] + ['elk_cleanup.sh'], calls)
                stages = json.loads((directory / 'stages.json').read_text())
                last = json.loads((directory / 'stages' / stages[-2] / 'result.json').read_text())
                self.assertEqual(7, last['exit_status'])
                self.assertIn('controlled', (directory / 'stages' / stages[-2] / 'output.log').read_text())
                self.assertEqual('cleanup', stages[-1])

    def test_cache_hits_preserve_complete_positive_evidence_legacy_then_modern(self):
        for profile_id in (2, 4):
            with self.subTest(profile=profile_id):
                self.env.update(EVSSIM_VERSIONS_CONFIGURATION_ID=str(profile_id), S7_TEST_CACHE='hit')
                process, directory = self.run_workflow()
                self.assertEqual(0, process.returncode, process.stdout + process.stderr)
                self.assertTrue(json.loads((directory / 'result.json').read_text())['passed'])
                self.assertEqual(list(workflow.STAGES), json.loads((directory / 'stages.json').read_text()))
                self.assertEqual('hit', json.loads((directory / 'artifact-selection.json').read_text())['cache'])

    def test_cleanup_failure_is_best_effort_and_does_not_replace_primary_status(self):
        for failure in ('compile-kernel.sh', ''):
            with self.subTest(failure=failure):
                self.env.update(S7_TEST_FAIL=failure, S7_TEST_CLEANUP_STATUS='9')
                process, directory = self.run_workflow()
                self.assertEqual(7 if failure else 0, process.returncode, process.stdout + process.stderr)
                record = json.loads((directory / 'stages/cleanup/result.json').read_text())
                self.assertEqual(9, record['exit_status'])

    def test_wrong_artifact_stops_at_launch_before_guest_tests(self):
        self.env.update(EVSSIM_VERSIONS_CONFIGURATION_ID='4', S7_TEST_BAD_ARTIFACT='version')
        process, directory = self.run_workflow()
        self.assertNotEqual(0, process.returncode)
        self.assertNotIn('docker-test-guest.sh|', self.log.read_text())
        self.assertEqual(['resolve-inputs', 'workflow-inputs', 'check-tools', 'tox', 'build-images', 'container-images',
            'qemu-sources', 'guest-image', 'kernel', 'qemu-artifact', 'host-tools', 'guest-tools', 'exofs-tools', 'sanity', 'cleanup'],
            json.loads((directory / 'stages.json').read_text()))

    def test_missing_or_changed_identity_output_and_stage_evidence_reject_success(self):
        process, directory = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        changes = [('workflow-inputs.json', lambda d: d['sources'].update(qemu='f' * 40)),
                   ('workflow-inputs.json', lambda d: d['profile']['kernel'].update(branch='changed')),
                   ('dependencies.json', lambda d: d.update(status='failed')),
                   ('artifact-selection.json', lambda d: d.pop('binary_sha256')),
                   ('artifact-selection.json', lambda d: d.update(version='0.0.0')),
                   ('artifact-manifest.json', lambda d: d.update(data_dir='missing')),
                   ('stages.json', lambda d: d.remove('exofs')),
                   ('stages/sanity/launches/1.json', lambda d: d['runtime'].update(runtime_executable='/wrong'))]
        for relative, change in changes:
            with self.subTest(path=relative):
                path = directory / relative; before = path.read_bytes()
                value = json.loads(before); change(value); path.write_text(json.dumps(value))
                try:
                    self.assertNotEqual(0, self.finish(directory).returncode)
                finally:
                    path.write_bytes(before)
        for relative in ('stages/guest-tests/result.json', 'stages/guest-tests/guest/2/guest_tests_results.xml',
                         'stages/guest-tests/guest/2/fio/current/test_write_short_write_res.fixture'):
            with self.subTest(missing=relative):
                path = directory / relative; before = path.read_bytes(); path.unlink()
                try:
                    self.assertNotEqual(0, self.finish(directory).returncode)
                finally:
                    path.write_bytes(before)

    def test_logging_failure_cannot_replace_child_exit_seven(self):
        directory = self.root / 'logging'; directory.mkdir()
        env = dict(self.env, EVSSIM_VERSIONS_CONFIGURATION_ID='2')
        original = Path.open
        class BrokenLog:
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def write(self, block): raise OSError('controlled log write failure')
        def opening(path, *args, **kwargs):
            return BrokenLog() if path.name == 'output.log' else original(path, *args, **kwargs)
        for child_status in (7, 0):
            with self.subTest(child_status=child_status):
                attempt = directory / str(child_status); attempt.mkdir()
                with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(Path, 'open', opening):
                    status = workflow.run_stage(attempt, 'kernel', [sys.executable, '-c',
                        "print('child'); raise SystemExit(" + str(child_status) + ")"])
                self.assertEqual(child_status or 1, status)
                record = json.loads((attempt / 'stages/kernel/result.json').read_text())
                self.assertEqual(child_status, record['child_exit_status'])
                self.assertTrue(record['logging_failed'])

    def test_lost_non_qemu_dependency_is_rejected_before_builds(self):
        revision = dependency_tests.git(self.root / 'workflow/kernel', 'rev-parse', 'HEAD')
        report = dict(schema_version=1, status='complete', dependencies=[dict(project='kernel',
            url='https://review.gerrithub.io/c/test/kernel/+/1', patch_set=1, revision=revision,
            ref='refs/changes/01/1/1', target_branch='master', applied_revision=revision,
            disposition='applied', error='')])
        path = self.root / 'dependencies.json'; path.write_text(json.dumps(report))
        self.env['EVSSIM_DEPENDENCY_RECORD'] = str(path)
        process, _ = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        report['dependencies'][0]['applied_revision'] = 'f' * 40
        path.write_text(json.dumps(report)); self.log.write_text('')
        process, directory = self.run_workflow()
        self.assertNotEqual(0, process.returncode)
        self.assertEqual(['elk_cleanup.sh'], [line.split('|')[0] for line in self.log.read_text().splitlines()])
        self.assertEqual(['resolve-inputs', 'workflow-inputs', 'cleanup'], json.loads((directory / 'stages.json').read_text()))

    def test_invalid_guest_result_or_measurement_rejects_success_even_with_updated_hashes(self):
        process, directory = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        folder = directory / 'stages/guest-tests'
        record_path = folder / 'result.json'
        record_bytes = record_path.read_bytes()
        for relative, value in (
                ('guest/1/guest_tests_results.xml', '<testsuite tests="1" failures="1"/>'),
                ('guest/2/fio/current/test_rw_short_write_res.fixture', 'write: IOPS=100\n'),
                ('guest/2/guest-tools.sha256', '0' * 64 + '  nvme\n'),
                ('guest/2/guest-kernel.txt', 'wrong-kernel')):
            with self.subTest(path=relative):
                path = folder / relative; before = path.read_bytes(); path.write_text(value)
                # Model bad evidence from its original producer, not only
                # corruption detected by the generic file checksum check.
                record = json.loads(record_bytes); record['files'][relative] = workflow.digest(path)
                record_path.write_text(json.dumps(record))
                try:
                    self.assertNotEqual(0, self.finish(directory).returncode)
                finally:
                    path.write_bytes(before); record_path.write_bytes(record_bytes)


class SourcePreparationTests(unittest.TestCase):
    def test_selected_sources_are_prepared_with_original_pins_and_failures_propagate(self):
        with tempfile.TemporaryDirectory(prefix='qemu-source-preparation-') as temporary:
            root = Path(temporary)
            shutil.copy2(BUILDER / 'prepare-qemu-sources.sh', root / 'prepare-qemu-sources.sh')
            (root / 'builder.sh').write_text('''evssim_validate_version_arguments() { return 0; }
evssim_run_at_folder() {
    printf '%s\\n' "$@" > "$PREPARATION_LOG"
    return "$PREPARATION_STATUS"
}
''')
            git = root / 'git'
            git.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$PREPARATION_LOG"\nexit "$PREPARATION_STATUS"\n')
            git.chmod(0o755)
            log = root / 'calls'
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ['PATH'],
                EVSSIM_ROOT_PATH=str(root), EVSSIM_QEMU_FOLDER='qemu',
                EVSSIM_DOCKER_ROOT_PATH='/code', EVSSIM_BUILDER_FOLDER='simulator/infra/builder',
                PREPARATION_LOG=str(log))
            for version in ('ubuntu-14.04', 'ubuntu-26.04'):
                for status in (0, 7):
                    with self.subTest(version=version, status=status):
                        env['PREPARATION_STATUS'] = str(status)
                        result = subprocess.run(['bash', './prepare-qemu-sources.sh', version],
                            cwd=root, env=env, capture_output=True, text=True, timeout=10)
                        self.assertEqual(status, result.returncode, result.stdout + result.stderr)
                        expected = (['-C', str(root / 'qemu'), 'submodule', 'update', '--init', '--recursive',
                            '--', 'dtc', 'capstone', 'ui/keycodemapdb'] if version == 'ubuntu-14.04' else
                            [version, 'qemu', 'python3', '/code/simulator/infra/builder/qemu-artifact.py',
                             '--prepare-subprojects', '.'])
                        self.assertEqual(expected, log.read_text().splitlines())


class GuestEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='workflow-guest-evidence-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def guest_script(self, test_status=0, collection_status=0, flush_status=0, stop_status=0, extraction_status=0):
        target = self.root / 'builder'; target.mkdir(exist_ok=True)
        shutil.copy2(BUILDER / 'docker-test-guest.sh', target / 'docker-test-guest.sh')
        (target / 'builder.sh').write_text('''set -eu -o pipefail
evssim_validate_version_arguments() { :; }
evssim_qemu_fresh_image() { :; }
evssim_qemu_detached() { :; }
evssim_wait_for_device() { :; }
evssim_guest() {
    if [[ "$*" == *nosetests* ]]; then
        echo test >> "$EVENTS"; return "$TEST_STATUS"
    fi
    echo collect >> "$EVENTS"; return "$COLLECTION_STATUS"
}
evssim_qemu_flush_disk() { echo flush >> "$EVENTS"; return "$FLUSH_STATUS"; }
evssim_qemu_stop() { echo stop >> "$EVENTS"; return "$STOP_STATUS"; }
evssim_run() {
    echo extract >> "$EVENTS"
    if [ "$EXTRACTION_STATUS" -ne 0 ]; then return "$EXTRACTION_STATUS"; fi
    directory="$EVSSIM_ROOT_PATH/${test_directory#/code/}/workflow"
    mkdir -p "$directory"
    echo '<testsuite tests="1" failures="0" errors="0"/>' > "$directory/guest_tests_results.xml"
}
''')
        stage = Path(tempfile.mkdtemp(dir=self.root, prefix='stage-'))
        events = stage / 'events'
        env = dict(os.environ, EVSSIM_ROOT_PATH=str(self.root), EVSSIM_DOCKER_ROOT_PATH='/code',
                   EVSSIM_DIST_FOLDER='dist', EVSSIM_QEMU_IMAGE='system.img', EVSSIM_LOGS_FOLDER='logs',
                   EVSSIM_GUEST_HOME_PATH='/home/esd', EVSSIM_PROVISIONED_DEVICE_COUNT='6',
                   EVSSIM_WORKFLOW_STAGE_DIR=str(stage), EVENTS=str(events), TEST_STATUS=str(test_status),
                   COLLECTION_STATUS=str(collection_status), FLUSH_STATUS=str(flush_status), STOP_STATUS=str(stop_status),
                   EXTRACTION_STATUS=str(extraction_status))
        result = subprocess.run(['bash', './docker-test-guest.sh', 'ubuntu-14.04'], cwd=target,
            env=env, capture_output=True, text=True, timeout=10)
        return result, stage, events.read_text().splitlines()

    def test_guest_test_failure_wins_over_collection_flush_and_stop_failures(self):
        result, _, events = self.guest_script(7, 9, 8, 6)
        self.assertEqual(7, result.returncode, result.stdout + result.stderr)
        self.assertEqual(['test', 'collect', 'flush', 'stop'], events)

    def test_guest_collection_or_flush_failure_stops_later_groups(self):
        for collection, flush in ((9, 0), (0, 7)):
            with self.subTest(collection=collection, flush=flush):
                result, _, events = self.guest_script(collection_status=collection, flush_status=flush)
                self.assertEqual(collection or flush, result.returncode, result.stdout + result.stderr)
                self.assertEqual(['test', 'collect', 'flush', 'stop', 'extract'], events)

    def test_successful_guest_groups_collect_before_image_replacement(self):
        result, stage, events = self.guest_script()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertEqual(['test', 'collect', 'flush', 'stop', 'extract'] * 4, events)
        for number in range(1, 5):
            self.assertTrue((stage / 'guest' / str(number) / 'guest_tests_results.xml').is_file())

    def test_guest_extraction_failure_stops_later_groups(self):
        result, _, events = self.guest_script(extraction_status=17)
        self.assertEqual(17, result.returncode, result.stdout + result.stderr)
        self.assertEqual(['test', 'collect', 'flush', 'stop', 'extract'], events)

    def test_guest_collector_preserves_junit_tools_and_dynamic_fio_paths(self):
        guest = self.root / 'guest'; guest.mkdir()
        (guest / 'guest_tests_results.xml').write_text('<testsuite tests="1"/>')
        for name in ('nvme', 'tnvme', 'dnvme.ko'):
            (guest / name).write_text('guest tool ' + name)
        (guest / 'fio_tests/reference_results').mkdir(parents=True)
        (guest / 'fio_tests/reference_results/test_write_short_ref_res').write_text('write: IOPS=100')
        (guest / 'fio_tests/fio_jobs').mkdir()
        (guest / 'fio_tests/fio_jobs/test_write_short.fio').write_text('rw=write')
        temporary = self.root / 'temporary results'; temporary.mkdir()
        (temporary / 'test_write_short_write_res.fixture').write_text('write: IOPS=100')
        (temporary / 'test_write_short_write_res.fixture').chmod(0o600)
        binary = self.root / 'bin'; binary.mkdir()
        (binary / 'python').write_text('#!/bin/sh\nexec ' + sys.executable + ' "$@"\n')
        (binary / 'python').chmod(0o755)
        script = BUILDER.parents[1] / 'eVSSIM/tests/guest/common/collect-workflow-evidence.sh'
        env = dict(os.environ, TMPDIR=str(temporary), PATH=str(binary) + os.pathsep + os.environ['PATH'])
        result = subprocess.run(['bash', str(script), 'fio_tests'], cwd=guest, env=env, capture_output=True, text=True)
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        copied = guest / 'Logs/workflow'
        self.assertEqual((temporary / 'test_write_short_write_res.fixture').read_bytes(),
                         (copied / 'fio/current/test_write_short_write_res.fixture').read_bytes())
        self.assertEqual(3, len((copied / 'guest-tools.sha256').read_text().splitlines()))
        self.assertEqual(0o044, (copied / 'fio/current/test_write_short_write_res.fixture').stat().st_mode & 0o044)
        (guest / 'guest_tests_results.xml').unlink()
        result = subprocess.run(['bash', str(script), 'fio_tests'], cwd=guest, env=env, capture_output=True, text=True)
        self.assertNotEqual(0, result.returncode)

    def test_launch_record_failure_is_not_masked_by_conditional_caller(self):
        text = (BUILDER / 'builder.sh').read_text()
        start = text.index('evssim_qemu_detached () {')
        function = text[start:text.index('\n}\n', start) + 3]
        shell = '''set -eu -o pipefail
evssim_qemu_default() { :; }
evssim_wait_for_guest_startup() { :; }
evssim_wait_for_devices() { echo devices; }
python3() { echo record; return "$RECORD_STATUS"; }
'''+function+'\nif evssim_qemu_detached fixture; then exit 0; else exit 7; fi\n'
        for status in (0, 9):
            with self.subTest(record_status=status):
                env = dict(os.environ, EVSSIM_QEMU_LAUNCH_SELECTION='', EVSSIM_WORKFLOW_STAGE_DIR=str(self.root),
                    EVSSIM_ROOT_PATH=str(self.root), EVSSIM_BUILDER_FOLDER='builder', EVSSIM_DATA_FOLDER='data',
                    EVSSIM_DOCKER_UUID='fixture', EVSSIM_PROVISIONED_DEVICE_COUNT='6', EVSSIM_RUNTIME_STORAGE_STRATEGY='1',
                    EVSSIM_QEMU_SIMULATOR_ENABLED='yes', RECORD_STATUS=str(status))
                result = subprocess.run(['bash', '-c', shell], env=env, capture_output=True, text=True)
                self.assertEqual(0 if status == 0 else 7, result.returncode, result.stdout + result.stderr)
                self.assertEqual(['devices', 'record'], result.stdout.splitlines())


class EvidenceExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='workflow-export-test-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.target, self.env, self.log = workflow_fixture(self.root)
        self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = '4'

    def run_and_export(self, failure='', allow_incomplete=False):
        self.env['S7_TEST_FAIL'] = failure
        result = subprocess.run(['bash', str(self.target / 'run-ci.sh')], cwd=self.root,
            env=self.env, capture_output=True, text=True, timeout=40)
        self.assertEqual(7 if failure else 0, result.returncode, result.stdout + result.stderr)
        workspace = self.root / 'workflow'
        run = next((workspace / 'logs/qemu11-validation').glob('run.*'))
        # Build sources and images must not enter the evidence archive.
        (run / 'qemu/workspace').mkdir(parents=True, exist_ok=True)
        (run / 'qemu/workspace/large-source').write_text('excluded')
        (run / 'system.img').write_text('excluded guest image')
        destination = workspace / 'qemu11-validation'
        destination.mkdir(exist_ok=True)
        (destination / 'caller.json').write_text(json.dumps({'completed': not bool(failure)}))
        durable = self.root / 'durable'
        with mock.patch.dict(os.environ, self.env, clear=True):
            status = workflow.export_evidence(workspace, destination, durable, allow_incomplete)
        return status, run, destination, durable

    def test_success_export_is_validated_durable_and_excludes_bulk_inputs(self):
        status, run, destination, durable = self.run_and_export()
        self.assertEqual(0, status)
        for copy in (destination / run.name, durable / run.name):
            self.assertTrue((copy / 'result.json').is_file())
            self.assertTrue((copy / 'stages/guest-tests/guest/2/guest_tests_results.xml').is_file())
            self.assertTrue((copy / 'artifact-manifest.json').is_file())
            self.assertFalse((copy / 'qemu/workspace/large-source').exists())
            self.assertFalse((copy / 'system.img').exists())
        index = json.loads((durable / 'export.json').read_text())
        for relative, checksum in index['files'].items():
            self.assertEqual(checksum, workflow.digest(durable / relative))
        shutil.rmtree(self.root / 'workflow')
        self.assertTrue((durable / run.name / 'result.json').is_file())

    def test_failure_export_retains_failed_diagnostics_and_marks_incomplete(self):
        status, run, _, durable = self.run_and_export('compile-kernel.sh', True)
        self.assertEqual(0, status)
        self.assertEqual(7, json.loads((durable / run.name / 'result.json').read_text())['exit_status'])
        self.assertTrue((durable / run.name / 'stages/kernel/output.log').is_file())
        self.assertTrue(json.loads((durable / 'export.json').read_text())['incomplete_allowed'])

    def test_corrupt_success_still_exports_diagnostics_but_returns_failure(self):
        _, run, destination, durable = self.run_and_export()
        (run / 'stages/sanity/output.log').write_text('changed after workflow validation')
        with mock.patch.dict(os.environ, self.env, clear=True):
            status = workflow.export_evidence(self.root / 'workflow', destination, durable)
        self.assertEqual(1, status)
        self.assertTrue((durable / run.name / 'stages/sanity/output.log').is_file())
        self.assertTrue(json.loads((durable / 'export.json').read_text())['errors'])

    def test_no_workflow_receipt_cannot_make_a_successful_caller_pass(self):
        workspace = self.root / 'workflow'
        destination = workspace / 'qemu11-validation'
        self.assertEqual(1, workflow.export_evidence(workspace, destination, self.root / 'durable'))
