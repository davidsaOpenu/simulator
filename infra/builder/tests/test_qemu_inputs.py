"""Test profile resolution and caller selection without compiling or booting."""

import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

import test_dependency_handler as dependency_tests


BUILDER = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qemu_inputs", BUILDER / "qemu-inputs.py")
inputs = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inputs)


class InputSelectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-inputs-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu"
        self.simulator = self.root / "simulator"
        for repo in (self.qemu, self.simulator):
            dependency_tests.new_repo(repo)
            dependency_tests.write(repo, "source", repo.name + "\n")
            dependency_tests.commit(repo, "source")
        self.profiles = inputs.read_json(BUILDER / "versions/configs.json")
        self.policy = inputs.read_json(BUILDER / "versions/qemu-build-policy.json")
        self.report = {"schema_version": 1, "status": "complete", "dependencies": []}
        self.dependency = {
            "project": "qemu", "url": "https://review.gerrithub.io/c/test/qemu/+/1",
            "patch_set": 7, "revision": "a" * 40, "ref": "refs/changes/01/1/7",
            "target_branch": "11.0", "applied_revision": dependency_tests.git(self.qemu, "rev-parse", "HEAD"),
            "disposition": "applied", "error": "",
        }

    def profile(self, profile_id):
        return next(p for p in self.profiles["configs"] if p["id"] == profile_id)

    def resolve(self, explicit=None):
        return inputs.resolve_inputs(self.report, self.profiles, self.policy,
                                     self.qemu, self.simulator, explicit)

    def modern_dependency(self):
        self.report["dependencies"] = [copy.deepcopy(self.dependency)]

    def cli(self, text=None, options=()):
        path = self.root / "report.json"
        path.write_text(json.dumps(self.report) if text is None else text)
        return subprocess.run([
            "bash", str(BUILDER / "compile-qemu.sh"), "--resolve-dependencies", str(path),
            "--qemu-dir", str(self.qemu), "--simulator-dir", str(self.simulator), *options,
        ], cwd=self.root, capture_output=True, text=True, timeout=15)

    def test_no_dependency_uses_legacy_resolver_default(self):
        resolved = self.resolve()
        self.assertEqual(self.profile(2), resolved["profile"])
        self.assertEqual("master", resolved["sources"]["qemu"]["target_branch"])
        self.assertEqual([], resolved["dependencies"])

    def test_eleven_dependency_selects_existing_profile_four(self):
        self.modern_dependency()
        resolved = self.resolve()
        self.assertEqual(self.profile(4), resolved["profile"])
        self.assertEqual({"branch": "11.0", "compileContainer": "ubuntu-26.04"},
                         resolved["profile"]["qemu"])
        self.assertEqual("ubuntu-14.04", resolved["profile"]["guestTests"]["compileContainer"])

    def test_explicit_compatible_selection_is_preserved(self):
        self.assertEqual(self.profile(5), self.resolve(5)["profile"])
        self.modern_dependency()
        self.assertEqual(self.profile(4), self.resolve(4)["profile"])

    def test_conflicting_explicit_profiles_fail(self):
        self.modern_dependency()
        for profile_id in (2, 5):
            with self.subTest(profile=profile_id), self.assertRaisesRegex(inputs.InputError, "conflicts"):
                self.resolve(profile_id)

    def test_invalid_or_missing_explicit_profile_fails(self):
        for profile_id in (True, 0, -1, "4", 999):
            with self.subTest(profile=profile_id), self.assertRaises(inputs.InputError):
                self.resolve(profile_id)

    def test_declared_and_applied_revisions_are_retained_with_actual_heads(self):
        self.modern_dependency()
        resolved = self.resolve()
        self.assertEqual(self.report["dependencies"], resolved["dependencies"])
        self.assertNotEqual(resolved["dependencies"][0]["revision"],
                            resolved["dependencies"][0]["applied_revision"])
        for name, repo in (("qemu", self.qemu), ("simulator", self.simulator)):
            self.assertEqual(dependency_tests.git(repo, "rev-parse", "HEAD"),
                             resolved["sources"][name]["revision"])

    def test_every_project_is_retained_without_overriding_qemu_selection(self):
        projects = ("qemu", "kernel", "dnvme", "nvme-cli", "tnvme", "open-osd", "simulator")
        self.report["dependencies"] = [dict(self.dependency, project=name,
            url=f"https://review.gerrithub.io/c/test/{name}/+/{number}",
            ref=f"refs/changes/{number:02d}/{number}/7")
            for number, name in enumerate(projects, 1)]
        self.assertEqual(self.report["dependencies"], self.resolve()["dependencies"])
        self.assertEqual(4, self.resolve()["profile"]["id"])

    def test_non_qemu_dependency_alone_retains_legacy_default(self):
        self.report["dependencies"] = [dict(self.dependency, project="kernel", target_branch="master",
            url="https://review.gerrithub.io/c/test/kernel/+/1")]
        resolved = self.resolve()
        self.assertEqual(2, resolved["profile"]["id"])
        self.assertEqual(self.report["dependencies"], resolved["dependencies"])

    def test_simulator_target_branch_comes_from_its_dependency(self):
        self.report["dependencies"] = [dict(self.dependency, project="simulator", target_branch="delivery",
            url="https://review.gerrithub.io/c/test/simulator/+/1")]
        self.assertEqual("delivery", self.resolve()["sources"]["simulator"]["target_branch"])

    def test_resolution_does_not_mutate_inputs(self):
        self.modern_dependency()
        before = copy.deepcopy((self.report, self.profiles, self.policy))
        resolved = self.resolve()
        resolved["profile"]["qemu"]["branch"] = "changed"
        resolved["dependencies"][0]["url"] = "changed"
        self.assertEqual(before, (self.report, self.profiles, self.policy))

    def test_additional_branch_profile_mapping_requires_only_data(self):
        future = copy.deepcopy(self.profile(4))
        future["id"] = 9
        future["qemu"] = {"branch": "future-target", "compileContainer": "future-container"}
        self.profiles["configs"].append(future)
        self.policy["qemu_branches"]["future-target"] = 9
        self.report["dependencies"] = [dict(self.dependency, target_branch="future-target")]
        self.assertEqual(future, self.resolve()["profile"])

    def test_failed_or_incomplete_report_cannot_resolve(self):
        for status in ("failed", "running", None):
            with self.subTest(status=status), self.assertRaises(inputs.InputError):
                self.report["status"] = status
                self.resolve()
        self.report["status"] = "complete"
        for version in (True, 0, 2):
            with self.subTest(version=version), self.assertRaises(inputs.InputError):
                self.report["schema_version"] = version
                self.resolve()

    def test_invalid_dependency_fields_cannot_resolve(self):
        changes = (("target_branch", None), ("ref", None), ("patch_set", 8),
                   ("applied_revision", ""), ("revision", "not-a-sha"),
                   ("disposition", "failed"), ("error", "application failed"), ("project", "unknown"))
        for field, value in changes:
            with self.subTest(field=field), self.assertRaises(inputs.InputError):
                self.report["dependencies"] = [dict(self.dependency, **{field: value})]
                self.resolve()

    def test_missing_dependency_fields_cannot_resolve(self):
        for field in self.dependency:
            with self.subTest(field=field), self.assertRaises(inputs.InputError):
                dependency = copy.deepcopy(self.dependency)
                del dependency[field]
                self.report["dependencies"] = [dependency]
                self.resolve()

    def test_duplicate_dependencies_and_conflicting_branches_fail(self):
        for second in (self.dependency, dict(self.dependency, url="another", target_branch="master")):
            with self.subTest(second=second), self.assertRaises(inputs.InputError):
                self.report["dependencies"] = [self.dependency, second]
                self.resolve()

    def test_unsupported_qemu_branch_fails(self):
        self.report["dependencies"] = [dict(self.dependency, target_branch="unsupported")]
        with self.assertRaisesRegex(inputs.InputError, "no profile policy"):
            self.resolve()

    def test_missing_or_ambiguous_profiles_fail(self):
        self.profiles["configs"].append(copy.deepcopy(self.profile(2)))
        with self.assertRaisesRegex(inputs.InputError, "unique"):
            self.resolve()
        self.profiles["configs"].pop()
        self.profiles["configs"] = [p for p in self.profiles["configs"] if p["id"] != 2]
        with self.assertRaisesRegex(inputs.InputError, "does not exist"):
            self.resolve()

    def test_incomplete_selected_profile_fails(self):
        self.modern_dependency()
        for section in ("qemu", "kernel", "nvme-cli", "hostTests", "guestTests"):
            with self.subTest(section=section):
                original = self.profile(4).pop(section)
                with self.assertRaises(inputs.InputError):
                    self.resolve()
                self.profile(4)[section] = original

    def test_invalid_policy_fails(self):
        original = copy.deepcopy(self.policy)
        for changes in ({"default_profile": True}, {"qemu_branches": []},
                        {"qemu_branches": {"master": 0}}, {"qemu_branches": {}}):
            with self.subTest(changes=changes), self.assertRaises(inputs.InputError):
                self.policy = dict(original, **changes)
                self.resolve()

    def test_missing_checkout_cannot_produce_a_resolved_record(self):
        self.qemu = self.root / "missing"
        with self.assertRaisesRegex(inputs.InputError, "cannot read source HEAD"):
            self.resolve()

    def test_shell_entrypoint_returns_exact_resolved_inputs(self):
        self.modern_dependency()
        before = [dependency_tests.git(repo, "status", "--porcelain") for repo in (self.qemu, self.simulator)]
        process = self.cli()
        self.assertEqual(0, process.returncode, process.stderr)
        self.assertEqual(self.resolve(), json.loads(process.stdout))
        self.assertIn("selected profile 4", process.stderr)
        self.assertEqual(before, [dependency_tests.git(repo, "status", "--porcelain")
                                  for repo in (self.qemu, self.simulator)])

    def test_cli_conflict_emits_no_success_json(self):
        self.modern_dependency()
        process = self.cli(options=("--profile-id", "2"))
        self.assertNotEqual(0, process.returncode)
        self.assertEqual("", process.stdout)
        self.assertIn("conflicts", process.stderr)

    def test_cli_rejects_invalid_json_and_failed_reports(self):
        for text in ("not JSON", '{"schema_version":1,"schema_version":1}',
                     json.dumps(dict(self.report, status="failed"))):
            with self.subTest(text=text):
                process = self.cli(text=text)
                self.assertNotEqual(0, process.returncode)
                self.assertEqual("", process.stdout)

    def test_s2a_report_handoff_keeps_all_seven_projects(self):
        fixture = dependency_tests.DependencyHandlerTests("test_report_retains_every_supported_project")
        fixture.setUp()
        try:
            fixture.test_report_retains_every_supported_project()
            report = json.loads(fixture.report_file.read_text())
            resolved = inputs.resolve_inputs(report, self.profiles, self.policy,
                                             fixture.qemu, fixture.root / "simulator")
            self.assertEqual(4, resolved["profile"]["id"])
            self.assertEqual(report["dependencies"], resolved["dependencies"])
            self.assertEqual(7, len(resolved["dependencies"]))
        finally:
            fixture.doCleanups()

    def test_env_defaults_unset_and_empty_ids_without_overriding_explicit_ids(self):
        for value, expected in ((None, "5"), ("", "5"), ("4", "4"), ("2", "2")):
            with self.subTest(value=value):
                env = dict(os.environ)
                env.pop("EVSSIM_VERSIONS_CONFIGURATION_ID", None)
                if value is not None:
                    env["EVSSIM_VERSIONS_CONFIGURATION_ID"] = value
                process = subprocess.run(["bash", "-c", 'set -e; source ./env.sh >/dev/null; '
                    'printf "%s" "$EVSSIM_VERSIONS_CONFIGURATION_ID"'], cwd=BUILDER,
                    env=env, capture_output=True, text=True, timeout=15)
                self.assertEqual(0, process.returncode, process.stderr)
                self.assertEqual(expected, process.stdout)

    def test_profile_five_declares_existing_guest_tools_and_original_nvme_cli(self):
        profile = self.profile(5)
        self.assertEqual({"branch": "3.0-a.5", "compileContainer": "ubuntu-26.04"}, profile["nvme-cli"])
        self.assertEqual({"guestVmImage": "ubuntu-14.04", "guestVmBuildContainer": "ubuntu-14.04",
                          "compileContainer": "ubuntu-14.04"}, profile["guestTests"])

    def test_compile_preflight_rejects_mismatched_containers_and_saved_records(self):
        env = dict(os.environ, EVSSIM_QEMU_BRANCH="11.0", EVSSIM_QEMU_COMPILE_CONTAINER="ubuntu-26.04",
                   EVSSIM_VERSIONS_CONFIGURATION_ID="4")
        env.pop("EVSSIM_QEMU_INPUT_RECORD", None)
        for version, record, message in (("ubuntu-14.04", None, "conflicts"),
                                         ("ubuntu-26.04", "saved-record", "belongs to S2c")):
            with self.subTest(version=version):
                if record:
                    env["EVSSIM_QEMU_INPUT_RECORD"] = record
                process = subprocess.run(["bash", str(BUILDER / "compile-qemu.sh"), version],
                    cwd=self.root, env=env, capture_output=True, text=True, timeout=15)
                self.assertNotEqual(0, process.returncode)
                self.assertIn(message, process.stderr)

    def test_version_wrapper_uses_selected_branch_instead_of_container_inference(self):
        target = self.root / "compiler"
        target.mkdir()
        shutil.copy2(BUILDER / "compile-qemu.sh", target / "compile-qemu.sh")
        (target / "builder.sh").write_text('''evssim_validate_version_arguments() { return 0; }
git() { printf 'git: %s\\n' "$*" >> "$S2B_TEST_CALL_LOG"; }
evssim_run_at_folder() { printf 'run: %s\\n' "$*" >> "$S2B_TEST_CALL_LOG"; }
''')
        prepare = self.simulator / "infra/builder/prepare-qemu-vssim.sh"
        prepare.parent.mkdir(parents=True)
        prepare.write_text('#!/bin/bash\nprintf "prepare: %s\\n" "$*" >> "$S2B_TEST_CALL_LOG"\n')
        prepare.chmod(0o755)
        log = self.root / "compile-calls.log"
        future = copy.deepcopy(self.profile(4))
        future["qemu"]["branch"] = "future-target"
        for profile in (self.profile(2), self.profile(4), future):
            with self.subTest(branch=profile["qemu"]["branch"]):
                log.write_text("")
                env = dict(os.environ, EVSSIM_ROOT_PATH=str(self.root), EVSSIM_SIMULATOR_FOLDER="simulator",
                    EVSSIM_QEMU_FOLDER="qemu", EVSSIM_DOCKER_ROOT_PATH="/code", EVSSIM_DIST_FOLDER="dist",
                    EVSSIM_KERNEL_FOLDER="kernel", EVSSIM_QEMU_BRANCH=profile["qemu"]["branch"],
                    EVSSIM_QEMU_COMPILE_CONTAINER=profile["qemu"]["compileContainer"],
                    EVSSIM_VERSIONS_CONFIGURATION_ID=str(profile["id"]), S2B_TEST_CALL_LOG=str(log))
                env.pop("EVSSIM_QEMU_INPUT_RECORD", None)
                process = subprocess.run(["bash", str(target / "compile-qemu.sh"),
                    profile["qemu"]["compileContainer"]], cwd=target, env=env,
                    capture_output=True, text=True, timeout=15)
                self.assertEqual(0, process.returncode, process.stderr)
                calls = log.read_text()
                self.assertIn("checkout " + profile["qemu"]["branch"], calls)
                self.assertIn("--target-list=x86_64-softmmu", calls)

    def workflow_fixture(self):
        target = self.root / "workflow/simulator/infra/builder"
        target.mkdir(parents=True, exist_ok=True)
        for name in ("run-ci.sh", "env.sh", "load_config.sh"):
            shutil.copy2(BUILDER / name, target / name)
        (target / "versions").mkdir(exist_ok=True)
        for name in ("configs.json", "qemu-build-policy.json"):
            shutil.copy2(BUILDER / "versions" / name, target / "versions" / name)
        stub = '''#!/bin/bash
printf '%s|%s|%s|%s|%s|%s\\n' "$(basename "$0")" "$EVSSIM_VERSIONS_CONFIGURATION_ID" "$EVSSIM_QEMU_BRANCH" "$EVSSIM_QEMU_COMPILE_CONTAINER" "$EVSSIM_GUEST_TESTS_COMPILE_CONTAINER" "$*" >> "$S2B_TEST_CALL_LOG"
'''
        scripts = ("check_tools.sh", "build-docker-image.sh", "build-qemu-image.sh", "compile-kernel.sh",
                   "compile-qemu.sh", "compile-host-tests.sh", "compile-guest-tests.sh", "docker-run-sanity.sh",
                   "docker-test-host.sh", "docker-test-host-elk.sh", "docker-test-guest.sh", "docker-test-exofs.sh")
        elk = target.parent / "ELK"
        elk.mkdir(exist_ok=True)
        tools = self.root / "bin"
        tools.mkdir(exist_ok=True)
        for path in ([target / name for name in scripts] + [tools / "tox"] +
                     [elk / name for name in ("install_and_start_elk.sh", "elk_cleanup.sh")]):
            path.write_text(stub)
            path.chmod(0o755)
        log = self.root / "calls.log"
        log.write_text("")
        env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"], S2B_TEST_CALL_LOG=str(log))
        for name in ("EVSSIM_VERSIONS_CONFIGURATION_ID", "EVSSIM_QEMU_INPUT_RECORD"):
            env.pop(name, None)
        return target, env, log

    def test_workflow_keeps_modern_compile_child_and_legacy_runtime(self):
        target, env, log = self.workflow_fixture()
        process = subprocess.run(["bash", str(target / "run-ci.sh")], env=env,
                                 capture_output=True, text=True, timeout=15)
        self.assertEqual(0, process.returncode, process.stdout + process.stderr)
        calls = [line.split("|") for line in log.read_text().splitlines()]
        builds = [c for c in calls if c[0] == "compile-qemu.sh"]
        self.assertEqual([("4", "11.0", "ubuntu-26.04", "ubuntu-26.04"),
                          ("5", "master", "ubuntu-14.04", "ubuntu-14.04")],
                         [(c[1], c[2], c[3], c[5]) for c in builds])
        self.assertEqual(["docker-run-sanity.sh", "docker-test-host.sh", "docker-test-host-elk.sh",
                          "docker-test-guest.sh", "docker-test-exofs.sh"],
                         [c[0] for c in calls if c[0].startswith("docker-")])
        self.assertTrue(all(c[1:4] == ["5", "master", "ubuntu-14.04"]
                            for c in calls if c[0].startswith("docker-")))
        self.assertEqual("ubuntu-14.04", next(c for c in calls if c[0] == "compile-guest-tests.sh")[5])
        self.assertEqual("ubuntu-26.04", next(c for c in calls if c[0] == "compile-host-tests.sh")[5])

    def test_workflow_rejects_modern_runtime_before_any_build(self):
        target, env, log = self.workflow_fixture()
        env["EVSSIM_VERSIONS_CONFIGURATION_ID"] = "4"
        process = subprocess.run(["bash", str(target / "run-ci.sh")], env=env,
                                 capture_output=True, text=True, timeout=15)
        self.assertNotEqual(0, process.returncode)
        self.assertIn("currently tests legacy QEMU", process.stderr)
        self.assertEqual("", log.read_text())

    def test_failed_policy_lookup_stops_before_qemu_compilation(self):
        for contents in ('{"default_profile":2,"qemu_branches":{}}', "not JSON"):
            with self.subTest(policy=contents):
                target, env, log = self.workflow_fixture()
                (target / "versions/qemu-build-policy.json").write_text(contents)
                process = subprocess.run(["bash", str(target / "run-ci.sh")], env=env,
                                         capture_output=True, text=True, timeout=15)
                self.assertNotEqual(0, process.returncode)
                self.assertNotIn("compile-qemu.sh|", log.read_text())


if __name__ == "__main__":
    unittest.main()
