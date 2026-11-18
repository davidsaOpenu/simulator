"""Exercise workflow status/evidence with controlled build and runtime boundaries."""
import importlib.util
import hashlib
import tarfile
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
import hashlib, importlib.util, io, json, os, pathlib, shutil, subprocess, sys, tarfile
name = pathlib.Path(sys.argv[0]).name
env = os.environ
with open(env['S2B_TEST_CALL_LOG'], 'a') as stream:
    stream.write('|'.join([name, env['EVSSIM_VERSIONS_CONFIGURATION_ID'], env['EVSSIM_QEMU_BRANCH'],
        env['EVSSIM_QEMU_COMPILE_CONTAINER'], env['EVSSIM_GUEST_TESTS_COMPILE_CONTAINER'],
        ' '.join(sys.argv[1:]), env.get('EVSSIM_QEMU_ARTIFACT_SELECTION', '')]) + '\n')
print('controlled output from ' + name, flush=True)
if env.get('S7_TEST_FAIL') == name:
    sys.exit(7)
dist = pathlib.Path(env['EVSSIM_ROOT_PATH']) / 'dist'
if name == 'compile-kernel.sh':
    (dist / 'kernel').mkdir(parents=True, exist_ok=True)
    (dist / 'kernel/kernelrelease').write_text('5.0.0+\n')
if name == 'compile-guest-tests.sh':
    guest = dist / 'tools/home/esd/guest'; guest.mkdir(parents=True, exist_ok=True)
    for tool in ('tnvme', 'dnvme.ko'): (guest / tool).write_text('controlled guest tool ' + tool)
if name == 'compile-nvme-cli.sh':
    nvme = dist / 'tools/usr/local/sbin/nvme'; nvme.parent.mkdir(parents=True, exist_ok=True)
    nvme.write_text('controlled guest tool nvme')
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
            if number == 1:
                payload = b'controlled raw compliance diagnostics\n'
                with tarfile.open(str(output / 'nvme-compliance-logs.tar.gz'), 'w:gz') as archive:
                    entry = tarfile.TarInfo('test1.txt'); entry.size = len(payload)
                    archive.addfile(entry, io.BytesIO(payload))
            dist = pathlib.Path(env['EVSSIM_ROOT_PATH']) / 'dist'
            payload = {'/usr/local/sbin/nvme': dist / 'tools/usr/local/sbin/nvme',
                       'tnvme': dist / 'tools/home/esd/guest/tnvme',
                       'dnvme.ko': dist / 'tools/home/esd/guest/dnvme.ko'}
            (output / 'guest-tools.sha256').write_text(''.join(hashlib.sha256(path.read_bytes()).hexdigest()
                + '  ' + name + '\n' for name, path in payload.items()))
            (output / 'guest-kernel.txt').write_text((dist / 'kernel/kernelrelease').read_text())
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
        logs = pathlib.Path(sys.argv[sys.argv.index('--build-dir') + 1]) / 'logs'
        logs.mkdir(parents=True); (logs / 'build.log').write_text('controlled failed compilation\n')
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
            target_branch=original['sources'][name]['target_branch'])
    inputs['sources']['qemu']['version'] = version
    for name in ('target', 'configure_args', 'build_environment'): inputs[name] = recipe[name]
    identity = dict(inputs=inputs, key=implementation.input_key(inputs), dependency_report=report)
    bundle = pathlib.Path(sys.argv[sys.argv.index('--build-dir') + 1]) / 'fixture-bundle'
    logs = bundle.parent / 'logs'; logs.mkdir(parents=True)
    (logs / 'build.log').write_text('controlled compilation\n')
    workspace = bundle.parent / 'workspace'; workspace.mkdir()
    (workspace / 'generated-link').symlink_to('/container-only/build-path')
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
               "compile-osd-exofs.sh", "compile-nvme-cli.sh", "compile-host-tests.sh", "compile-guest-tests.sh", "docker-run-sanity.sh",
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
                 "BUILD_TAG", "GERRIT_PATCHSET_REVISION",
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

    def test_existing_jenkins_commands_share_report_across_separate_shells(self):
        # Keep Git real and control only Gerrit transport and the existing
        # build/runtime boundaries. Neither command receives a report path.
        handler = dependency_tests.DependencyHandlerTests()
        handler.setUp()
        self.addCleanup(handler.doCleanups)
        workspace = self.target.parents[2]
        dependency_tests.git(workspace / 'qemu', 'remote', 'add', 'origin', str(handler.origin))
        handler.response_file.write_text(json.dumps(handler.responses))
        for key, value in handler.env.items():
            if key.startswith('DEPENDENCY_TEST_') or key.startswith('GIT_CONFIG_'):
                self.env[key] = value
        self.env['PATH'] = str(handler.bin) + os.pathsep + self.env['PATH']
        self.env.pop('EVSSIM_VERSIONS_CONFIGURATION_ID')
        dependency_tests.git(workspace / 'simulator', 'commit', '--allow-empty', '-m', handler.responses['message'])
        self.env.update(BUILD_TAG='jenkins-simulator-12', GERRIT_CHANGE_NUMBER='99',
                        GERRIT_PATCHSET_REVISION=dependency_tests.git(workspace / 'simulator', 'rev-parse', 'HEAD'))
        shutil.copy2(BUILDER / 'handle-depend-on-instructions.sh', self.target)
        process = subprocess.run(['bash', './handle-depend-on-instructions.sh', str(workspace),
            '99', 'test', 'gerrit'], cwd=self.target, env=self.env, capture_output=True, text=True, timeout=30)
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        self.assertNotIn('EVSSIM_DEPENDENCY_RECORD', self.env)
        process, directory = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        report = json.loads((directory / 'dependencies.json').read_text())
        self.assertEqual(handler.review_revision, report['dependencies'][0]['revision'])
        self.assertEqual(2, json.loads((directory / 'workflow-inputs.json').read_text())['profile']['id'])

        # A different build, change or patch set must not reuse that report.
        for key, value in (('BUILD_TAG', 'jenkins-simulator-13'), ('GERRIT_CHANGE_NUMBER', '100'),
                           ('GERRIT_PATCHSET_REVISION', 'b' * 40)):
            with self.subTest(context=key):
                original = self.env[key]
                self.env[key] = value
                self.log.write_text('')
                process, _ = self.run_workflow()
                self.assertNotEqual(0, process.returncode)
                self.assertEqual(['elk_cleanup.sh'], [line.split('|')[0] for line in self.log.read_text().splitlines()])
                self.env[key] = original

        # Explicit reports still override discovery, even without a matching
        # automatic file for this build.
        self.env['BUILD_TAG'] = 'jenkins-simulator-13'
        self.env['EVSSIM_DEPENDENCY_RECORD'] = str(directory / 'dependencies.json')
        process, _ = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)

    def test_automatic_report_failures_stop_builds_legacy_then_modern(self):
        self.env.pop('EVSSIM_VERSIONS_CONFIGURATION_ID')
        self.env.update(BUILD_TAG='jenkins-simulator-12', GERRIT_CHANGE_NUMBER='99',
                        GERRIT_PATCHSET_REVISION='a' * 40)
        key = hashlib.sha256(('jenkins-simulator-12\n99\n' + 'a' * 40 + '\n').encode()).hexdigest()
        path = self.root / 'workflow/logs/qemu11-validation' / ('dependencies-' + key + '.json')
        path.parent.mkdir(parents=True)
        revision = dependency_tests.git(self.root / 'workflow/qemu', 'rev-parse', 'HEAD')
        dependency = dict(project='qemu', url='https://review.gerrithub.io/c/test/qemu/+/1',
            patch_set=1, revision=revision, ref='refs/changes/01/1/1', target_branch='11.0',
            applied_revision=revision, disposition='applied', error='')
        for profile_id, dependencies in ((2, []), (4, [dependency])):
            self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = str(profile_id)
            report = dict(schema_version=1, status='complete', dependencies=dependencies)
            path.write_text(json.dumps(report))
            process, directory = self.run_workflow()
            self.assertEqual(0, process.returncode, process.stdout + process.stderr)
            self.assertEqual(profile_id, json.loads((directory / 'workflow-inputs.json').read_text())['profile']['id'])
            # These are consumer failures, even if the unchanged Jenkins
            # caller continues here after a failed dependency stage.
            invalid = (None, '', '{', json.dumps(dict(report, status='incomplete')),
                       json.dumps(dict(report, status='failed')),
                       json.dumps(dict(schema_version=1, status='complete')),
                       json.dumps(dict(report, dependencies=[dict(dependency, disposition='failed', error='fetch failed')])),
                       json.dumps(dict(report, dependencies=[dict(dependency, applied_revision='f' * 40)])))
            for text in invalid:
                with self.subTest(profile=profile_id, report=text):
                    if text is None:
                        path.unlink()
                    else:
                        path.write_text(text)
                    self.log.write_text('')
                    process, directory = self.run_workflow()
                    self.assertNotEqual(0, process.returncode, process.stdout + process.stderr)
                    self.assertFalse(json.loads((directory / 'result.json').read_text())['passed'])
                    self.assertEqual(['elk_cleanup.sh'], [line.split('|')[0] for line in self.log.read_text().splitlines()])

        # Dependency metadata leaves the configured legacy lane unchanged.
        path.write_text(json.dumps(report))
        self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = '2'
        self.log.write_text('')
        process, directory = self.run_workflow()
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        self.assertEqual(2, json.loads((directory / 'workflow-inputs.json').read_text())['profile']['id'])

    def test_every_required_child_failure_preserves_seven_and_stops_later_stages(self):
        commands = ['check_tools.sh', 'tox', 'build-docker-image.sh', 'prepare-qemu-sources.sh', 'build-qemu-image.sh', 'compile-kernel.sh',
                    'qemu-artifact.py', 'compile-host-tests.sh', 'compile-guest-tests.sh', 'compile-osd-exofs.sh', 'compile-nvme-cli.sh',
                    'docker-run-sanity.sh', 'install_and_start_elk.sh', 'docker-test-host.sh',
                    'docker-test-host-elk.sh', 'docker-test-guest.sh', 'docker-test-exofs.sh']
        for profile in ('2', '4'):
            self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = profile
            for failed in commands:
                with self.subTest(profile=profile, command=failed):
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

    def test_build_staging_is_excluded_from_copyable_logs_and_diagnostics_survive_failure(self):
        for profile in ('2', '4'):
            for failure in ('qemu-artifact.py', ''):
                with self.subTest(profile=profile, failure=failure):
                    self.env.update(EVSSIM_VERSIONS_CONFIGURATION_ID=profile, S7_TEST_FAIL=failure)
                    result, directory = self.run_workflow()
                    self.assertEqual(7 if failure else 0, result.returncode, result.stdout + result.stderr)
                    self.assertIn('controlled', (directory / 'qemu/logs/build.log').read_text())
                    destination = self.root / 'copied-logs' / directory.name
                    # Like scp -r, dereference links; the controlled build link
                    # would fail this copy if staging were still under logs.
                    shutil.copytree(self.root / 'workflow/logs', destination)
                    build = self.root / 'workflow/artifacts/qemu-builds' / directory.name
                    self.assertTrue((build / 'logs/build.log').is_file())
                    if not failure:
                        self.assertTrue((build / 'workspace/generated-link').is_symlink())
                    self.assertFalse((directory / 'qemu/workspace').exists())

    def test_cleanup_failure_is_best_effort_and_does_not_replace_primary_status(self):
        for failure in ('compile-kernel.sh', ''):
            with self.subTest(failure=failure):
                self.env.update(S7_TEST_FAIL=failure, S7_TEST_CLEANUP_STATUS='9')
                process, directory = self.run_workflow()
                self.assertEqual(7 if failure else 0, process.returncode, process.stdout + process.stderr)
                record = json.loads((directory / 'stages/cleanup/result.json').read_text())
                self.assertEqual(9, record['exit_status'])

    def test_automatic_exports_retain_each_run_after_workspace_replacement(self):
        copies = []
        for failure in ('compile-kernel.sh', ''):
            self.env['S7_TEST_FAIL'] = failure
            process, directory = self.run_workflow()
            self.assertEqual(7 if failure else 0, process.returncode, process.stdout + process.stderr)
            durable = self.root / 'workflow@qemu11-validation' / directory.name
            self.assertIn('INFO Durable validation evidence: ' + str(durable), process.stdout)
            result = durable / directory.name / 'result.json'
            self.assertEqual(7 if failure else 0, json.loads(result.read_text())['exit_status'])
            index = json.loads((durable / 'export.json').read_text())
            self.assertEqual(bool(failure), index['incomplete_allowed'])
            self.assertEqual({directory.name}, {Path(name).parts[0] for name in index['files']})
            for name, checksum in index['files'].items():
                self.assertEqual(checksum, workflow.digest(durable / name))
            copies.append((durable, directory.name))
        # Removing only the disposable fixture's workspace models the existing
        # caller's next checkout; both independent evidence copies must survive.
        shutil.rmtree(self.root / 'workflow')
        failed, successful = copies
        self.assertTrue((failed[0] / failed[1] / 'stages/kernel/output.log').is_file())
        self.assertTrue((successful[0] / successful[1] / 'stages/guest-tests/guest/2/guest_tests_results.xml').is_file())
        self.assertTrue((successful[0] / successful[1] / 'stages/guest-tests/guest/2/fio/current/test_write_short_write_res.fixture').is_file())

    def test_export_failure_fails_success_but_preserves_required_failure(self):
        # A file where the sibling directory belongs makes the actual copy
        # fail without depending on root/non-root permission differences.
        (self.root / 'workflow@qemu11-validation').write_text('blocked destination')
        for failure in ('compile-kernel.sh', ''):
            with self.subTest(failure=failure):
                self.env['S7_TEST_FAIL'] = failure
                process, directory = self.run_workflow()
                self.assertEqual(7 if failure else 1, process.returncode, process.stdout + process.stderr)
                self.assertTrue((directory / 'export/export.json').is_file())
                self.assertIn('ERROR Workflow evidence:', process.stderr)

    def test_wrong_artifact_stops_at_launch_before_guest_tests(self):
        self.env.update(EVSSIM_VERSIONS_CONFIGURATION_ID='4', S7_TEST_BAD_ARTIFACT='version')
        process, directory = self.run_workflow()
        self.assertNotEqual(0, process.returncode)
        self.assertNotIn('docker-test-guest.sh|', self.log.read_text())
        self.assertEqual(['resolve-inputs', 'workflow-inputs', 'check-tools', 'tox', 'build-images', 'container-images',
            'qemu-sources', 'guest-image', 'kernel', 'qemu-artifact', 'host-tools', 'guest-tools', 'exofs-tools', 'nvme-cli', 'sanity', 'cleanup'],
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
                         'stages/guest-tests/guest/1/nvme-compliance-logs.tar.gz',
                         'stages/guest-tests/guest/2/fio/current/test_write_short_write_res.fixture'):
            with self.subTest(missing=relative):
                path = directory / relative; before = path.read_bytes(); path.unlink()
                try:
                    self.assertNotEqual(0, self.finish(directory).returncode)
                finally:
                    path.write_bytes(before)

    def test_each_guest_group_requires_execution_and_allows_partial_skips(self):
        for profile in ('2', '4'):
            with self.subTest(profile=profile):
                self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = profile
                process, directory = self.run_workflow()
                self.assertEqual(0, process.returncode, process.stdout + process.stderr)
                folder = directory / 'stages/guest-tests'
                record_path = folder / 'result.json'
                record = json.loads(record_path.read_text())
                for number in range(1, 5):
                    relative = 'guest/%d/guest_tests_results.xml' % number
                    path = folder / relative
                    original = path.read_bytes()
                    for attribute in ('skip="1"', 'skipped="1"', ''):
                        for executed in (False, True):
                            with self.subTest(group=number, attribute=attribute, executed=executed):
                                # Keep producer checksums consistent so this
                                # exercises execution counts, not corruption.
                                passing = '<testcase name="ran"/>' if executed else ''
                                skipped = ('<testcase name="skipped"/>' if attribute else
                                           '<testcase name="skipped"><skipped/></testcase>')
                                path.write_text('<testsuite tests="%d" errors="0" failures="0" %s>'
                                    '%s%s</testsuite>' % (2 if executed else 1, attribute, skipped, passing))
                                record['files'][relative] = workflow.digest(path)
                                record_path.write_text(json.dumps(record))
                                checked = self.finish(directory)
                                self.assertEqual(0 if executed else 1, checked.returncode,
                                                 checked.stdout + checked.stderr)
                                if not executed:
                                    self.assertIn('no executed tests', checked.stderr)
                    path.write_bytes(original)
                    record['files'][relative] = workflow.digest(path)
                    record_path.write_text(json.dumps(record))

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

    def test_changed_or_missing_staged_tools_and_kernel_release_reject_success(self):
        for profile in ('2', '4'):
            self.env['EVSSIM_VERSIONS_CONFIGURATION_ID'] = profile
            process, directory = self.run_workflow()
            self.assertEqual(0, process.returncode, process.stdout + process.stderr)
            dist = self.target.parents[2] / 'dist'
            for name in ('tools/usr/local/sbin/nvme', 'tools/home/esd/guest/tnvme',
                         'tools/home/esd/guest/dnvme.ko', 'kernel/kernelrelease'):
                path = dist / name
                original = path.read_bytes()
                for replacement in (None, b'wrong staged input'):
                    with self.subTest(profile=profile, path=name, missing=replacement is None):
                        if replacement is None:
                            path.unlink()
                        else:
                            path.write_bytes(replacement)
                        try:
                            self.assertNotEqual(0, self.finish(directory).returncode)
                        finally:
                            path.write_bytes(original)
                self.assertEqual(0, self.finish(directory).returncode)

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
        kernel = self.root / 'kernel'
        if not kernel.exists(): dependency_tests.new_repo(kernel)
        shutil.copy2(BUILDER / 'docker-test-guest.sh', target / 'docker-test-guest.sh')
        (target / 'builder.sh').write_text('''set -eu -o pipefail
evssim_validate_version_arguments() { :; }
evssim_qemu_fresh_image() { :; }
evssim_qemu_detached() { :; }
evssim_wait_for_device() { :; }
evssim_guest() {
    printf '%s\\n' "$*" >> "$GUEST_COMMANDS"
    if [[ "$*" == *nosetests* ]]; then
        echo test >> "$EVENTS"; return "$TEST_STATUS"
    fi
    echo collect >> "$EVENTS"; return "$COLLECTION_STATUS"
}
evssim_qemu_flush_disk() { echo flush >> "$EVENTS"; return "$FLUSH_STATUS"; }
evssim_qemu_stop() { echo stop >> "$EVENTS"; return "$STOP_STATUS"; }
evssim_run() {
    printf '%s\\n' "$*" > "$EXTRACTION_COMMAND"
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
                   EVSSIM_KERNEL_FOLDER=str(kernel), EVSSIM_GUESTFS_DOCKER_IMAGE='ubuntu-26.04',
                   EVSSIM_EXTERNAL_UID=str(os.getuid()), EVSSIM_EXTERNAL_GID=str(os.getgid()),
                   GUEST_COMMANDS=str(stage / 'guest-commands'), EXTRACTION_COMMAND=str(stage / 'extraction-command'),
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
        commands = (stage / 'guest-commands').read_text().splitlines()
        self.assertEqual(4, sum('--xunit-file=/home/esd/guest/guest_tests_results.xml' in c for c in commands))
        self.assertEqual(4, sum('nosetests3 -v -s' in c for c in commands))
        self.assertTrue((stage / 'extraction-command').read_text().startswith('ubuntu-26.04 '))
        self.assertIn('sudo chown -R ' + str(os.getuid()) + ':' + str(os.getgid()),
                      (stage / 'extraction-command').read_text())

    def test_guest_extraction_failure_stops_later_groups(self):
        result, _, events = self.guest_script(extraction_status=17)
        self.assertEqual(17, result.returncode, result.stdout + result.stderr)
        self.assertEqual(['test', 'collect', 'flush', 'stop', 'extract'], events)

    def collector_environment(self, guest):
        binary = self.root / 'collector-bin'; binary.mkdir(exist_ok=True)
        real_sha = shutil.which('sha256sum')
        nvme = guest / 'nvme'
        (binary / 'sha256sum').write_text("#!/usr/bin/env python3\n" +
            "import subprocess, sys\n" +
            "args = [" + repr(str(nvme)) + " if a == '/usr/local/sbin/nvme' else a for a in sys.argv[1:]]\n" +
            "r = subprocess.run([" + repr(real_sha) + "] + args, capture_output=True, text=True)\n" +
            "sys.stdout.write(r.stdout.replace(" + repr(str(nvme)) + ", '/usr/local/sbin/nvme'))\n" +
            "sys.stderr.write(r.stderr); sys.exit(r.returncode)\n")
        (binary / 'sha256sum').chmod(0o755)
        (binary / 'python').write_text('#!/bin/sh\nexit 99\n')
        (binary / 'python').chmod(0o755)
        return dict(os.environ, PATH=str(binary) + os.pathsep + os.environ['PATH'])

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
        script = BUILDER.parents[1] / 'eVSSIM/tests/guest/common/collect-workflow-evidence.sh'
        env = dict(self.collector_environment(guest), TMPDIR=str(temporary))
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

    def test_guest_collector_preserves_raw_compliance_output_after_pass_or_failure(self):
        guest = self.root / 'guest'; guest.mkdir()
        logs = guest / 'Logs'; logs.mkdir()
        for name in ('nvme', 'tnvme', 'dnvme.ko'):
            (guest / name).write_text('controlled tool')
        raw = {'test%d.txt' % n: ('suite %d diagnostics\n' % n).encode() for n in range(1, 28)}
        raw['kdump12.txt'] = b'optional kernel diagnostics\n'
        for name, data in raw.items():
            (logs / name).write_bytes(data)
        script = BUILDER.parents[1] / 'eVSSIM/tests/guest/common/collect-workflow-evidence.sh'
        env = self.collector_environment(guest)
        for failures in (0, 1):
            with self.subTest(failures=failures):
                xml = '<testsuite tests="1" errors="0" failures="%d"/>' % failures
                (guest / 'guest_tests_results.xml').write_text(xml)
                result = subprocess.run(['bash', str(script), 'nvme_compliance_tests'],
                    cwd=guest, env=env, capture_output=True, text=True)
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                output = logs / 'workflow'
                self.assertEqual(xml, (output / 'guest_tests_results.xml').read_text())
                archive_path = output / 'nvme-compliance-logs.tar.gz'
                with tarfile.open(archive_path) as archive:
                    contents = {m.name.removeprefix('./'): archive.extractfile(m).read()
                                for m in archive.getmembers() if m.isfile()}
                self.assertEqual(raw, contents)
                self.assertEqual(0o044, archive_path.stat().st_mode & 0o044)
                self.assertEqual(raw, {name: (logs / name).read_bytes() for name in raw})

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
            with tarfile.open(copy / 'stages/guest-tests/guest/1/nvme-compliance-logs.tar.gz') as archive:
                self.assertEqual(b'controlled raw compliance diagnostics\n', archive.extractfile('test1.txt').read())
            self.assertFalse((copy / 'qemu/workspace/large-source').exists())
            self.assertFalse((copy / 'system.img').exists())
        index = json.loads((durable / 'export.json').read_text())
        for relative, checksum in index['files'].items():
            self.assertEqual(checksum, workflow.digest(durable / relative))
        shutil.rmtree(self.root / 'workflow')
        self.assertTrue((durable / run.name / 'result.json').is_file())
        self.assertTrue((durable / run.name / 'stages/guest-tests/guest/1/nvme-compliance-logs.tar.gz').is_file())

    def test_failure_export_retains_failed_diagnostics_and_marks_incomplete(self):
        status, run, _, durable = self.run_and_export('compile-kernel.sh', True)
        self.assertEqual(0, status)
        self.assertEqual(7, json.loads((durable / run.name / 'result.json').read_text())['exit_status'])
        self.assertTrue((durable / run.name / 'stages/kernel/output.log').is_file())
        self.assertTrue(json.loads((durable / 'export.json').read_text())['incomplete_allowed'])

    def test_failed_workflow_keeps_collected_compliance_archive(self):
        _, run, _, _ = self.run_and_export()
        destination = self.root / 'failed-export'
        durable = self.root / 'failed-durable'
        with mock.patch.dict(os.environ, self.env, clear=True):
            self.assertEqual(7, workflow.finish(run, 7))
            self.assertEqual(0, workflow.export_evidence(self.root / 'workflow', destination,
                                                       durable, allow_incomplete=True, run=run))
        relative = run.name + '/stages/guest-tests/guest/1/nvme-compliance-logs.tar.gz'
        shutil.rmtree(self.root / 'workflow')
        with tarfile.open(durable / relative) as archive:
            self.assertEqual(b'controlled raw compliance diagnostics\n', archive.extractfile('test1.txt').read())

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
