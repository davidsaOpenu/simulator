import copy
from concurrent.futures import ThreadPoolExecutor
import errno
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import unittest
from unittest import mock


HELPER = Path(__file__).resolve().parents[1] / "qemu-artifact.py"
SPEC = importlib.util.spec_from_file_location("qemu_artifact", str(HELPER))
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)
IMAGE = {"image_id": "sha256:" + "a" * 64, "os": "linux",
         "architecture": "amd64", "variant": ""}


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo)] + list(args),
                                   stderr=subprocess.PIPE).decode().strip()


def write(repo, name, content):
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def new_repo(path):
    path.mkdir()
    git(path, "init", "-q")
    git(path, "config", "user.name", "Artifact test")
    git(path, "config", "user.email", "artifact-test@example.invalid")
    git(path, "config", "commit.gpgsign", "false")


def commit(repo, *paths):
    git(repo, "add", "--", *paths)
    git(repo, "commit", "-q", "-m", "fixture")
    return git(repo, "rev-parse", "HEAD")


class InputFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-artifact-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu"
        self.simulator = self.root / "simulator"
        for repo in (self.qemu, self.simulator):
            new_repo(repo)
        write(self.qemu, "source.c", "qemu input\n")
        write(self.qemu, "VERSION", "11.0.0\n")
        qemu_revision = commit(self.qemu, "source.c", "VERSION")
        for recipe in artifact.RECIPE_FILES:
            content = HELPER.read_text() if recipe.endswith("qemu-artifact.py") else recipe + "\n"
            write(self.simulator, recipe, content)
        write(self.simulator, "eVSSIM/ssd.c", "simulator input\n")
        simulator_revision = commit(self.simulator, *artifact.RECIPE_FILES, "eVSSIM/ssd.c")
        self.request = {
            "sources": {
                "qemu": {"revision": qemu_revision, "target_branch": "11.0"},
                "simulator": {"revision": simulator_revision, "target_branch": "master"},
            },
            "profile": {
                "id": 4,
                "qemu": {"branch": "11.0", "compileContainer": "ubuntu-26.04"},
                "kernel": {"branch": "master", "compileContainer": "ubuntu-14.04"},
                "nvme-cli": {"branch": "master", "compileContainer": "ubuntu-14.04"},
                "hostTests": {"compileContainer": "ubuntu-14.04", "runContainer": "ubuntu-14.04"},
                "guestTests": {"guestVmImage": "ubuntu-14.04", "guestVmBuildContainer": "ubuntu-14.04",
                               "compileContainer": "ubuntu-14.04"},
            },
            "container_image": "evssim:ubuntu-26.04",
            "target": "x86_64-softmmu",
            "configure_args": ["--enable-werror", "--disable-docs"],
            "build_environment": {},
            "dependencies": [{
                "source": "qemu", "url": "https://review.gerrithub.io/c/test/qemu/+/1",
                "patch_set": 7, "revision": "b" * 40,
                "applied_revision": qemu_revision, "target_branch": "11.0",
            }],
        }
        self.image = mock.patch.object(artifact, "inspect_image", return_value=copy.deepcopy(IMAGE))
        self.image_mock = self.image.start()
        self.addCleanup(self.image.stop)

    def identify(self, request=None):
        return artifact.identify(request or self.request, str(self.qemu), str(self.simulator))


class SelectionFixture(InputFixture):
    def setUp(self):
        super().setUp()
        self.profiles = json.loads((HELPER.parent / "versions/configs.json").read_text())
        self.policy = {"default_profile": 2, "qemu_branches": {"master": 2, "11.0": 4},
                       "builds": {"ubuntu-26.04": {name: copy.deepcopy(self.request[name]) for name in
                                  ("container_image", "target", "configure_args", "build_environment")}}}
        self.policy["builds"]["ubuntu-14.04"] = dict(
            self.policy["builds"]["ubuntu-26.04"], container_image="evssim:14.04",
            configure_args=["--enable-vssim"])
        dependency = self.request["dependencies"][0]
        self.report = {"schema_version": 1, "status": "complete", "dependencies": [{
            "project": "qemu", **{name: dependency[name] for name in
                ("url", "patch_set", "revision", "applied_revision", "target_branch")},
            "ref": "refs/changes/01/1/7", "disposition": "applied", "error": "",
        }]}

    def resolve(self, explicit=None):
        return artifact.resolve_request(self.report, self.profiles, self.policy,
                                        str(self.qemu), str(self.simulator), explicit)


class ResolutionTests(SelectionFixture):
    def test_no_dependency_selects_legacy_default_and_recorded_head(self):
        self.report["dependencies"] = []
        resolved = self.resolve()
        self.assertEqual(2, resolved["profile"]["id"])
        self.assertEqual({"revision": git(self.qemu, "rev-parse", "HEAD"), "target_branch": "master"},
                         resolved["sources"]["qemu"])
        self.assertEqual("evssim:14.04", resolved["container_image"])
        self.assertEqual(["--enable-vssim"], resolved["configure_args"])

    def test_qemu_dependency_resolves_exact_inputs_and_preserves_declared_applied_ids(self):
        resolved = self.resolve()
        identified = self.identify(resolved)
        self.assertEqual(self.request, resolved)
        self.assertEqual(resolved["profile"], identified["inputs"]["profile"])
        self.assertEqual(resolved["dependencies"], identified["inputs"]["dependencies"])
        self.assertNotEqual(resolved["dependencies"][0]["revision"],
                            resolved["dependencies"][0]["applied_revision"])

    def test_compatible_explicit_profile_is_retained_and_conflict_is_rejected(self):
        self.assertEqual(1, self.resolve(1)["profile"]["id"])
        self.assertEqual(6, self.resolve(6)["profile"]["id"])
        with self.assertRaisesRegex(artifact.InputError, "conflicts"):
            self.resolve(2)
        with self.assertRaisesRegex(artifact.InputError, "conflicts"):
            self.resolve(5)
        self.report["dependencies"] = []
        self.assertEqual(1, self.resolve(1)["profile"]["id"])

    def test_profile_declaring_legacy_qemu_is_rejected_for_eleven_dependency(self):
        self.policy["qemu_branches"]["11.0"] = 5
        with self.assertRaisesRegex(artifact.InputError, "conflicts"):
            self.resolve()

    def test_checked_in_policy_declares_legacy_inputs_and_separate_qemu_eleven_profile(self):
        self.policy = json.loads((HELPER.parent / "versions/qemu-build-policy.json").read_text())
        selected = self.resolve()["profile"]
        self.assertEqual(6, selected["id"])
        self.assertEqual({"branch": "11.0", "compileContainer": "ubuntu-26.04"}, selected["qemu"])
        # Declare the existing legacy workflow's effective inputs in the
        # profile, so artifact identity and actual build choices agree.
        expected_five = {
            "id": 5,
            "kernel": {"branch": "master", "compileContainer": "ubuntu-14.04"},
            "qemu": {"branch": "master", "compileContainer": "ubuntu-14.04"},
            "nvme-cli": {"branch": "3.0-a.5", "compileContainer": "ubuntu-14.04"},
            "hostTests": {"compileContainer": "ubuntu-26.04", "runContainer": "ubuntu-26.04"},
            "guestTests": {"guestVmImage": "ubuntu-14.04", "guestVmBuildContainer": "ubuntu-14.04",
                           "compileContainer": "ubuntu-14.04"},
        }
        legacy = next(profile for profile in self.profiles["configs"] if profile["id"] == 5)
        self.assertEqual(expected_five, legacy)
        for section in ("kernel", "nvme-cli", "guestTests", "hostTests"):
            self.assertEqual(expected_five[section], selected[section])

    def test_additional_branch_profile_and_container_need_only_data_changes(self):
        additional = copy.deepcopy(self.profiles["configs"][3])
        additional["id"] = 9
        additional["qemu"] = {"branch": "future-target", "compileContainer": "extra-container"}
        self.profiles["configs"].append(additional)
        self.policy["qemu_branches"]["future-target"] = 9
        self.policy["builds"]["extra-container"] = self.policy["builds"]["ubuntu-26.04"]
        self.report["dependencies"][0]["target_branch"] = "future-target"
        resolved = self.resolve()
        self.assertEqual(additional, resolved["profile"])
        self.assertEqual("future-target", resolved["sources"]["qemu"]["target_branch"])

    def test_failed_incomplete_or_inconsistent_metadata_cannot_resolve(self):
        original = copy.deepcopy(self.report)
        changes = [lambda r: r.update(status="failed"), lambda r: r.update(status="running"),
                   lambda r: r["dependencies"][0].update(disposition="failed", error="application failed"),
                   lambda r: r["dependencies"][0].pop("target_branch"),
                   lambda r: r["dependencies"][0].update(patch_set=8),
                   lambda r: r["dependencies"][0].update(applied_revision=""),
                   lambda r: r["dependencies"][0].update(target_branch="unsupported"),
                   lambda r: r["dependencies"].append(dict(r["dependencies"][0], url="other", target_branch="master"))]
        for change in changes:
            with self.subTest(change=changes.index(change)):
                self.report = copy.deepcopy(original)
                change(self.report)
                with self.assertRaises(artifact.InputError):
                    self.resolve()

    def test_unapplied_success_record_is_rejected_by_artifact_identity(self):
        self.report["dependencies"][0]["applied_revision"] = "c" * 40
        with self.assertRaisesRegex(artifact.InputError, "not reachable"):
            self.identify(self.resolve())

    def test_missing_ambiguous_or_incomplete_profiles_and_recipes_are_rejected(self):
        original = copy.deepcopy(self.profiles)
        for corrupt in (lambda: self.profiles["configs"].append(copy.deepcopy(self.profiles["configs"][0])),
                        lambda: self.profiles["configs"][3].pop("guestTests"),
                        lambda: self.profiles["configs"][3].update(qemu="invalid")):
            self.profiles = copy.deepcopy(original)
            corrupt()
            with self.assertRaises(artifact.InputError):
                self.resolve()
        self.profiles = original
        with self.assertRaisesRegex(artifact.InputError, "does not exist"):
            self.resolve(99)
        self.policy["builds"].clear()
        with self.assertRaisesRegex(artifact.InputError, "no build recipe"):
            self.resolve()

    def test_resolution_does_not_mutate_metadata_profiles_or_policy(self):
        original = copy.deepcopy((self.report, self.profiles, self.policy))
        resolved = self.resolve()
        resolved["profile"]["qemu"]["branch"] = "changed"
        resolved["configure_args"].append("changed")
        self.assertEqual(original, (self.report, self.profiles, self.policy))

    def test_shell_entrypoint_resolves_and_stops_before_docker_on_invalid_metadata(self):
        paths = [self.root / name for name in ("dependencies.json", "profiles.json", "policy.json")]
        for path, data in zip(paths, (self.report, self.profiles, self.policy)):
            path.write_text(json.dumps(data))
        docker = self.root / "bin/docker"
        docker.parent.mkdir()
        docker.write_text("#!" + sys.executable + "\nimport json, pathlib, sys\n"
                          "assert sys.argv[1:3] == ['image', 'inspect']\n"
                          "pathlib.Path(" + repr(str(self.root / "docker-called")) + ").touch()\n"
                          "print(json.dumps(" + repr({"Id": IMAGE["image_id"], "Os": "linux",
                                                     "Architecture": "amd64"}) + "))\n")
        docker.chmod(0o755)
        args = ["bash", str(HELPER.parent / "compile-qemu.sh"), "--resolve-dependencies", str(paths[0]),
                "--profiles", str(paths[1]), "--selection-policy", str(paths[2]),
                "--qemu-dir", str(self.qemu), "--simulator-dir", str(self.simulator)]
        env = dict(os.environ, PATH=str(docker.parent) + os.pathsep + os.environ["PATH"])
        success = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(0, success.returncode, success.stderr.decode())
        self.assertEqual(self.identify(self.resolve()), json.loads(success.stdout))
        self.assertIn(b"selected profile 4 for QEMU 11.0", success.stderr)
        (self.root / "docker-called").unlink()
        self.report["status"] = "failed"
        paths[0].write_text(json.dumps(self.report))
        failure_args = args + ["--build-dir", str(self.root / "must-not-build"),
                               "--artifact-root", str(self.root / "artifacts"),
                               "--dist-root", str(self.root / "dist")]
        failure = subprocess.run(failure_args, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(0, failure.returncode)
        self.assertEqual(b"", failure.stdout)
        self.assertFalse((self.root / "docker-called").exists())
        self.assertFalse((self.root / "must-not-build").exists())
        self.assertFalse((self.root / "artifacts").exists())


class DependencyRecordTests(SelectionFixture):
    def setUp(self):
        super().setUp()
        self.origin = self.root / "origin-qemu.git"
        git(self.qemu, "branch", "11.0")
        subprocess.check_output(["git", "clone", "--bare", str(self.qemu), str(self.origin)],
                                stderr=subprocess.PIPE)
        git(self.qemu, "remote", "add", "origin", str(self.origin))
        write(self.qemu, "source.c", "review content\n")
        self.review_revision = commit(self.qemu, "source.c")
        self.review_ref = "refs/changes/01/1/7"
        git(self.qemu, "push", "origin", "HEAD:" + self.review_ref)
        git(self.qemu, "checkout", "11.0")
        write(self.qemu, "base-extra.c", "new target-branch content\n")
        commit(self.qemu, "base-extra.c")
        git(self.qemu, "push", "origin", "HEAD:refs/heads/11.0")
        url = self.request["dependencies"][0]["url"]
        self.responses = {"message": "S2 fixture\n\nDepends-on: " + url,
                          "changes": {"1": {"status": "NEW", "branch": "11.0",
                              "current_revision": self.review_revision,
                              "revisions": {self.review_revision: {"ref": self.review_ref}}}}}
        self.response_file = self.root / "responses.json"
        self.report_file = self.root / "dependencies.json"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        scripts = {
            "curl": '''import json, os, re, sys
data = json.load(open(os.environ['QEMU_RECORD_TEST_DATA']))
url = sys.argv[-1]
if url.endswith('/commit'):
    if data.get('commit_rest_failure'):
        sys.exit(22)
    if data.get('commit_rest_http_status', 200) >= 400 and '-f' in sys.argv:
        sys.exit(22)
    print(data.get('commit_rest_response', json.dumps({'message': data['message']})))
    sys.exit(data.get('commit_rest_exit_status', 0))
else:
    change = re.search(r'~([0-9]+)/detail', url).group(1)
    if change in data.get('rest_failures', []):
        sys.exit(22)
    print(json.dumps(data['changes'].get(change, {})))
''',
            "git": '''import os, subprocess, sys
args = sys.argv[1:]
import json
failure = json.loads(os.environ.get('QEMU_RECORD_TEST_GIT_FAILURE', 'null'))
if failure and args[:len(failure)] == failure:
    sys.exit(1)
if len(args) > 1 and args[0] == 'fetch' and args[1].startswith('https://'):
    args[1] = os.environ['QEMU_RECORD_TEST_ORIGIN']
sys.exit(subprocess.call([os.environ['QEMU_RECORD_TEST_GIT']] + args))
''',
            "ssh": '''import json, os, sys
data = json.load(open(os.environ['QEMU_RECORD_TEST_DATA']))
if os.environ.get('QEMU_RECORD_TEST_SSH_LOG'):
    with open(os.environ['QEMU_RECORD_TEST_SSH_LOG'], 'a') as log:
        log.write(json.dumps(sys.argv[1:]) + '\\n')
change = sys.argv[-1].removeprefix('change:')
if change == '99' and '--commit-message' not in sys.argv:
    sys.exit(2)
response = data.get('ssh_changes', {}).get(change)
if response is None:
    sys.exit(1)
if isinstance(response, str):
    print(response)
else:
    for record in response if isinstance(response, list) else [response]:
        print(json.dumps(record))
sys.exit(data.get('ssh_exit_status', {}).get(change, 0))
''',
        }
        for name, source in scripts.items():
            path = self.bin / name
            path.write_text("#!" + sys.executable + "\n" + source)
            path.chmod(0o755)
        config = self.root / "global.gitconfig"
        config.write_text("[user]\n name = Record fixture\n email = record@example.invalid\n")
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        GIT_CONFIG_GLOBAL=str(config), GIT_CONFIG_NOSYSTEM="1",
                        QEMU_RECORD_TEST_GIT=shutil.which("git"),
                        QEMU_RECORD_TEST_DATA=str(self.response_file),
                        QEMU_RECORD_TEST_ORIGIN=str(self.origin),
                        EVSSIM_DEPENDENCY_RECORD=str(self.report_file))

    def handler(self, source="gerrit", with_record=True):
        self.response_file.write_text(json.dumps(self.responses))
        env = dict(self.env)
        if not with_record:
            env.pop("EVSSIM_DEPENDENCY_RECORD")
        return subprocess.run(["bash", str(HELPER.parent / "handle-depend-on-instructions.sh"),
                               str(self.root), "99", "test", source], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def prepend_dependency(self, project="qemu"):
        detail = copy.deepcopy(self.responses["changes"]["1"])
        self.responses["changes"]["2"] = detail
        self.responses["message"] = self.responses["message"].replace(
            "Depends-on:", f"Depends-on: https://review.gerrithub.io/c/test/{project}/+/2\nDepends-on:")
        return detail

    def assert_failed_then_applied(self, process, with_record=True):
        self.assertNotEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.assertIn(b"DEPENDENCY PROCESSING SUMMARY", process.stdout)
        self.assertIn(b"Change 1 - was applied", process.stdout)
        if with_record:
            report = json.loads(self.report_file.read_text())
            self.assertEqual("failed", report["status"])
            self.assertEqual(["failed", "applied"], [d["disposition"] for d in report["dependencies"]])

    def test_real_cherry_pick_record_resolves_into_artifact_identity(self):
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("complete", self.report["status"])
        dependency = self.report["dependencies"][0]
        self.assertEqual("applied", dependency["disposition"])
        self.assertEqual(self.review_revision, dependency["revision"])
        self.assertEqual(git(self.qemu, "rev-parse", "HEAD"), dependency["applied_revision"])
        self.assertNotEqual(dependency["revision"], dependency["applied_revision"])
        self.assertEqual((7, self.review_ref, "11.0"),
                         (dependency["patch_set"], dependency["ref"], dependency["target_branch"]))
        identified = self.identify(self.resolve())
        self.assertEqual(dependency["applied_revision"], identified["inputs"]["sources"]["qemu"]["revision"])
        self.assertEqual(4, identified["inputs"]["profile"]["id"])

    def test_existing_ancestor_records_its_original_revision(self):
        git(self.origin, "update-ref", "refs/heads/11.0", self.review_revision)
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.report = json.loads(self.report_file.read_text())
        dependency = self.report["dependencies"][0]
        self.assertEqual("already_present", dependency["disposition"])
        self.assertEqual(self.review_revision, dependency["applied_revision"])
        self.identify(self.resolve())

    def test_inferred_patch_set_ref_cannot_be_published_as_exact_metadata(self):
        git(self.origin, "update-ref", "refs/changes/01/1/1", self.review_revision)
        self.responses["changes"]["1"]["revisions"][self.review_revision]["ref"] = None
        self.responses["ssh_changes"] = {"1": {
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision}}}
        self.assertNotEqual(0, self.handler().returncode)
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])
        self.assertIn("Exact Gerrit patch-set ref", self.report["dependencies"][0]["error"])
        self.assertNotEqual(0, self.handler(with_record=False).returncode)

    def test_fetched_content_must_match_the_declared_review_revision(self):
        git(self.qemu, "checkout", "-b", "other-review", self.review_revision)
        write(self.qemu, "other-review.c", "different review content\n")
        other_revision = commit(self.qemu, "other-review.c")
        git(self.qemu, "checkout", "11.0")
        detail = self.responses["changes"]["1"]
        detail["current_revision"] = other_revision
        detail["revisions"] = {other_revision: {"ref": self.review_ref}}
        previous = git(self.qemu, "rev-parse", "HEAD")
        self.assertNotEqual(0, self.handler().returncode)
        self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])
        self.assertIn("Fetched revision differs", self.report["dependencies"][0]["error"])
        self.assertNotEqual(0, self.handler(with_record=False).returncode)
        self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))

    def test_malformed_dependency_cannot_be_reported_as_no_dependencies(self):
        self.responses["message"] = "S2 fixture\n\nDepends-on: missing-url"
        self.assertNotEqual(0, self.handler().returncode)
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])
        with self.assertRaises(artifact.InputError):
            self.resolve()

    def test_failed_dependency_followed_by_success_keeps_failed_record(self):
        abandoned = self.prepend_dependency()
        abandoned["status"] = "ABANDONED"
        self.assert_failed_then_applied(self.handler())
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])
        self.assertEqual(["failed", "applied"], [d["disposition"] for d in self.report["dependencies"]])
        with self.assertRaisesRegex(artifact.InputError, "incomplete or failed"):
            self.resolve()
        self.image_mock.assert_not_called()

    def test_failed_dependency_followed_by_success_returns_failure_without_record(self):
        self.prepend_dependency()["status"] = "ABANDONED"
        self.assert_failed_then_applied(self.handler(with_record=False), with_record=False)
        self.assertFalse(self.report_file.exists())

    def test_empty_cherry_pick_succeeds_and_continues_to_later_dependency(self):
        self.assertEqual(0, self.handler().returncode)
        applied = git(self.qemu, "rev-parse", "HEAD")
        git(self.qemu, "push", "origin", "HEAD:refs/heads/11.0")
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        report = json.loads(self.report_file.read_text())
        self.assertEqual("complete", report["status"])
        self.assertEqual("already_present", report["dependencies"][0]["disposition"])
        self.responses["message"] += "\nDepends-on: https://review.gerrithub.io/c/test/qemu/+/2"
        detail = copy.deepcopy(self.responses["changes"]["1"])
        detail["status"] = "ABANDONED"
        self.responses["changes"]["2"] = detail
        process = self.handler()
        self.assertNotEqual(0, process.returncode)
        report = json.loads(self.report_file.read_text())
        self.assertEqual(["already_present", "failed"], [d["disposition"] for d in report["dependencies"]])
        self.assertEqual(applied, report["dependencies"][0]["applied_revision"])
        self.assertIn(b"Empty cherry-pick skipped", process.stdout)
        self.assertIn(b"Change 2 - was not applied because it was abandoned", process.stdout)
        self.assertEqual("", git(self.qemu, "status", "--porcelain"))
        self.assertFalse((self.qemu / ".git/CHERRY_PICK_HEAD").exists())

    def test_empty_cherry_pick_returns_success_without_record(self):
        self.assertEqual(0, self.handler(with_record=False).returncode)
        git(self.qemu, "push", "origin", "HEAD:refs/heads/11.0")
        process = self.handler(with_record=False)
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.assertIn(b"Empty cherry-pick skipped", process.stdout)
        self.assertFalse((self.qemu / ".git/CHERRY_PICK_HEAD").exists())

    def test_empty_cherry_pick_skip_failure_is_not_success(self):
        self.assertEqual(0, self.handler().returncode)
        git(self.qemu, "push", "origin", "HEAD:refs/heads/11.0")
        self.env["QEMU_RECORD_TEST_GIT_FAILURE"] = json.dumps(["cherry-pick", "--skip"])
        process = self.handler()
        self.assertNotEqual(0, process.returncode)
        report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", report["status"])
        self.assertEqual("failed", report["dependencies"][0]["disposition"])

    def test_fetch_failure_does_not_hide_later_success(self):
        detail = self.prepend_dependency()
        detail["revisions"][self.review_revision]["ref"] = "refs/changes/02/2/8"
        self.assert_failed_then_applied(self.handler())

    def test_conflict_is_aborted_before_later_success(self):
        git(self.qemu, "checkout", "-b", "conflicting-review", self.review_revision + "^")
        write(self.qemu, "base-extra.c", "conflicting target-branch content\n")
        revision = commit(self.qemu, "base-extra.c")
        ref = "refs/changes/02/2/8"
        git(self.qemu, "push", "origin", "HEAD:" + ref)
        git(self.qemu, "checkout", "11.0")
        detail = self.prepend_dependency()
        detail["current_revision"] = revision
        detail["revisions"] = {revision: {"ref": ref}}
        self.assert_failed_then_applied(self.handler())
        self.assertFalse((self.qemu / ".git/CHERRY_PICK_HEAD").exists())
        self.assertEqual("", git(self.qemu, "status", "--porcelain"))
        self.assertEqual("new target-branch content\n", (self.qemu / "base-extra.c").read_text())
        self.assertEqual("review content\n", (self.qemu / "source.c").read_text())

    def test_missing_project_repository_does_not_run_git_in_parent_directory(self):
        self.prepend_dependency(project="kernel")
        self.assert_failed_then_applied(self.handler())
        self.assertFalse((self.root / ".git").exists())

    def test_invalid_project_does_not_hide_later_success(self):
        self.prepend_dependency(project="unexpected")
        self.assert_failed_then_applied(self.handler())

    def test_malformed_trailer_does_not_hide_later_success_without_record(self):
        self.responses["message"] = "Depends-on: missing-url\n" + self.responses["message"]
        process = self.handler(with_record=False)
        self.assertNotEqual(0, process.returncode)
        self.assertIn(b"Malformed Depends-on trailer", process.stdout)
        self.assertIn(b"Change 1 - was applied", process.stdout)

    def test_required_metadata_failures_return_failure_without_record(self):
        original = copy.deepcopy(self.responses["changes"]["1"])
        invalid = [dict(original, status="UNKNOWN"), dict(original, current_revision=None),
                   dict(original, branch=None), dict(original, branch="invalid branch"),
                   dict(original, revisions={}), {}]
        previous = git(self.qemu, "rev-parse", "HEAD")
        for detail in invalid:
            with self.subTest(detail=detail):
                self.responses["changes"]["1"] = detail
                process = self.handler(with_record=False)
                self.assertNotEqual(0, process.returncode, process.stdout.decode())
                self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))

    def test_target_branch_preparation_failures_return_failure(self):
        previous = git(self.qemu, "rev-parse", "HEAD")
        for command in (["fetch", "origin"], ["checkout", "-B"], ["branch", "--set-upstream-to=origin/11.0"]):
            with self.subTest(command=command):
                self.env["QEMU_RECORD_TEST_GIT_FAILURE"] = json.dumps(command)
                process = self.handler()
                self.assertNotEqual(0, process.returncode)
                report = json.loads(self.report_file.read_text())
                self.assertEqual("failed", report["status"])
                self.assertEqual("failed", report["dependencies"][0]["disposition"])
                self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))

    def test_rest_failure_uses_exact_ssh_metadata(self):
        self.responses["rest_failures"] = ["1"]
        self.responses["ssh_changes"] = {"1": [{
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision, "ref": self.review_ref}}, {"type": "stats", "rowCount": 1}]}
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                process = self.handler(with_record=with_record)
                self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
                self.assertEqual("11.0", git(self.qemu, "branch", "--show-current"))
                self.assertEqual("review content\n", (self.qemu / "source.c").read_text())
                if with_record:
                    report = json.loads(self.report_file.read_text())
                    self.assertEqual("complete", report["status"])
                    self.assertEqual("applied", report["dependencies"][0]["disposition"])

    def test_failed_ssh_metadata_output_cannot_apply_a_dependency(self):
        self.responses["rest_failures"] = ["1"]
        self.responses["ssh_changes"] = {"1": [{
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision, "ref": self.review_ref}}, {"type": "stats", "rowCount": 1}]}
        self.responses["ssh_exit_status"] = {"1": 255}
        previous = git(self.qemu, "rev-parse", "HEAD")
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                process = self.handler(with_record=with_record)
                self.assertNotEqual(0, process.returncode, process.stdout.decode())
                self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))
                self.assertEqual("qemu input\n", (self.qemu / "source.c").read_text())
                if with_record:
                    report = json.loads(self.report_file.read_text())
                    self.assertEqual("failed", report["status"])
                    self.assertEqual("failed", report["dependencies"][0]["disposition"])
                    self.assertEqual("SSH metadata retrieval failed (exit 255)",
                                     report["dependencies"][0]["error"])

    def test_failed_ssh_metadata_followed_by_success_keeps_failure(self):
        self.prepend_dependency()
        reference = "refs/changes/02/2/7"
        git(self.origin, "update-ref", reference, self.review_revision)
        self.responses["rest_failures"] = ["2"]
        self.responses["ssh_changes"] = {"2": {
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision, "ref": reference}}}
        self.responses["ssh_exit_status"] = {"2": 255}
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                self.assert_failed_then_applied(self.handler(with_record=with_record), with_record=with_record)

    def use_ssh_commit_message(self, message):
        self.responses["message"] = None
        self.responses["ssh_changes"] = {"99": [
            {"number": "99", "commitMessage": message},
            {"type": "stats", "rowCount": 1},
        ]}

    def rest_commit_message_failure_cases(self):
        valid_body = json.dumps({"message": "Untrusted response without dependency trailers"})
        cases = [
            ("transport failure after valid body", {"commit_rest_response": valid_body,
                                                    "commit_rest_exit_status": 7}),
            ("HTTP failure with valid body", {"commit_rest_response": valid_body,
                                              "commit_rest_http_status": 500}),
            ("empty response", {"commit_rest_response": ""}),
            ("malformed JSON", {"commit_rest_response": "not JSON"}),
            ("malformed trailing JSON", {"commit_rest_response": valid_body + "\nnot JSON"}),
            ("multiple JSON objects", {"commit_rest_response": valid_body + "\n" + valid_body}),
            ("missing message", {"commit_rest_response": "{}"}),
        ]
        for name, message in (("null", None), ("empty", ""), ("whitespace", " \n\t"),
                              ("numeric", 123), ("object", {"text": "message"}),
                              ("array", ["message"]), ("boolean", True)):
            cases.append((name + " message", {"commit_rest_response": json.dumps({"message": message})}))
        return cases

    def test_failed_or_invalid_rest_commit_message_uses_ssh_fallback(self):
        cases = self.rest_commit_message_failure_cases()
        message = self.responses["message"]
        self.use_ssh_commit_message(message)
        original = copy.deepcopy(self.responses)
        for name, response in cases:
            for with_record in (True, False):
                with self.subTest(reply=name, with_record=with_record):
                    self.responses = dict(original, **response)
                    process = self.handler(with_record=with_record)
                    self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
                    self.assertIn(b"Trying alternative method using SSH", process.stdout)
                    self.assertIn(b"Change 1 - was applied", process.stdout)
                    self.assertEqual("review content\n", (self.qemu / "source.c").read_text())
                    if with_record:
                        report = json.loads(self.report_file.read_text())
                        self.assertEqual("complete", report["status"])
                        self.assertEqual("applied", report["dependencies"][0]["disposition"])

    def test_failed_or_invalid_rest_commit_message_cannot_succeed_without_ssh(self):
        original = copy.deepcopy(self.responses)
        previous = git(self.qemu, "rev-parse", "HEAD")
        for name, response in self.rest_commit_message_failure_cases():
            for with_record in (True, False):
                with self.subTest(reply=name, with_record=with_record):
                    self.responses = dict(original, **response)
                    if with_record:
                        self.report_file.write_text(json.dumps({"schema_version": 1,
                            "status": "complete", "dependencies": []}))
                    process = self.handler(with_record=with_record)
                    self.assertNotEqual(0, process.returncode, process.stdout.decode())
                    self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))
                    self.assertEqual("qemu input\n", (self.qemu / "source.c").read_text())
                    if with_record:
                        self.assertEqual("failed", json.loads(self.report_file.read_text())["status"])

    def test_rest_commit_message_handles_xssi_prefix_and_preserves_dependency_trailers(self):
        self.responses["commit_rest_response"] = ")]}'\n" + json.dumps({"message": self.responses["message"]})
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                process = self.handler(with_record=with_record)
                self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
                self.assertNotIn(b"Trying alternative method using SSH", process.stdout)
                self.assertIn(b"Change 1 - was applied", process.stdout)
                self.assertEqual("review content\n", (self.qemu / "source.c").read_text())

    def test_ssh_commit_message_preserves_dependency_trailers(self):
        message = "S2 fixture\n\nMultiline body.\n\nDepends-on: " + self.request["dependencies"][0]["url"]
        self.use_ssh_commit_message(message)
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                self.responses["commit_rest_failure"] = True
                process = self.handler(with_record=with_record)
                self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
                self.assertIn(message.encode(), process.stdout)
                self.assertIn(b"Change 1 - was applied", process.stdout)
                self.assertEqual("review content\n", (self.qemu / "source.c").read_text())
                if with_record:
                    self.report = json.loads(self.report_file.read_text())
                    self.assertEqual("complete", self.report["status"])
                    self.assertEqual("applied", self.report["dependencies"][0]["disposition"])
                    self.assertEqual(self.review_revision, self.report["dependencies"][0]["revision"])
                    self.assertEqual(4, self.resolve()["profile"]["id"])

    def test_both_ssh_fallbacks_use_agent_account_without_changing_repository_owner(self):
        url = self.request["dependencies"][0]["url"]
        self.use_ssh_commit_message(self.responses["message"])
        self.responses["rest_failures"] = ["1"]
        self.responses["ssh_changes"]["1"] = {
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision, "ref": self.review_ref}}
        calls = self.root / "ssh-calls.jsonl"
        self.env["QEMU_RECORD_TEST_SSH_LOG"] = str(calls)
        for account in (None, "", "hazifer"):
            with self.subTest(agent_account=account):
                self.env.pop("GERRIT_OWNER_USERNAME", None)
                if account is not None:
                    self.env["GERRIT_OWNER_USERNAME"] = account
                calls.write_text("")
                process = self.handler()
                self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
                commands = [json.loads(line) for line in calls.read_text().splitlines()]
                self.assertEqual(2, len(commands))
                self.assertEqual([f"{account or 'test'}@review.gerrithub.io"] * 2,
                                 [command[2] for command in commands])
                self.assertEqual(["change:99", "change:1"], [command[-1] for command in commands])
                dependency = json.loads(self.report_file.read_text())["dependencies"][0]
                self.assertEqual(url, dependency["url"])
                self.assertEqual("qemu", dependency["project"])
                self.assertEqual(self.review_revision, dependency["revision"])

    def test_ssh_commit_message_retains_failed_then_successful_dependencies(self):
        self.prepend_dependency()["status"] = "ABANDONED"
        self.use_ssh_commit_message(self.responses["message"])
        for with_record in (True, False):
            with self.subTest(with_record=with_record):
                self.assert_failed_then_applied(self.handler(with_record=with_record), with_record)

    def test_ssh_commit_message_without_dependencies_is_valid(self):
        self.use_ssh_commit_message("S2 fixture\n\nBody without dependency trailers.")
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.assertEqual({"schema_version": 1, "status": "complete", "dependencies": []},
                         json.loads(self.report_file.read_text()))

    def test_ssh_commit_message_rejects_failed_or_invalid_replies(self):
        valid = {"number": "99", "commitMessage": self.responses["message"]}
        stats = {"type": "stats", "rowCount": 0}
        cases = [
            ("command failed after partial output", [valid, stats], 1),
            ("malformed JSON", "not JSON", 0),
            ("malformed trailing JSON", json.dumps(valid) + "\nnot JSON", 0),
            ("empty response", [], 0),
            ("stats only", [stats], 0),
            ("missing message", [{"number": "99"}, stats], 0),
            ("null message", [dict(valid, commitMessage=None), stats], 0),
            ("empty message", [dict(valid, commitMessage=""), stats], 0),
            ("whitespace message", [dict(valid, commitMessage=" \n\t"), stats], 0),
            ("non-string message", [dict(valid, commitMessage=123), stats], 0),
            ("ambiguous changes", [valid, valid, stats], 0),
        ]
        previous = git(self.qemu, "rev-parse", "HEAD")
        self.responses["message"] = None
        for name, response, status in cases:
            for with_record in (True, False):
                with self.subTest(reply=name, with_record=with_record):
                    self.responses["ssh_changes"] = {"99": response}
                    self.responses["ssh_exit_status"] = {"99": status}
                    if with_record:
                        self.report_file.write_text(json.dumps({"schema_version": 1,
                            "status": "complete", "dependencies": []}))
                    process = self.handler(with_record=with_record)
                    self.assertNotEqual(0, process.returncode, process.stdout.decode())
                    self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))
                    self.assertEqual("qemu input\n", (self.qemu / "source.c").read_text())
                    if with_record:
                        self.assertEqual("failed", json.loads(self.report_file.read_text())["status"])

    def test_no_dependencies_returns_success(self):
        self.responses["message"] = "S2 fixture without dependency trailers"
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stderr.decode())
        self.assertEqual({"schema_version": 1, "status": "complete", "dependencies": []},
                         json.loads(self.report_file.read_text()))

    def test_timer_produces_complete_empty_record_without_changing_sources(self):
        previous = git(self.qemu, "rev-parse", "HEAD")
        process = self.handler("timer")
        self.assertEqual(0, process.returncode, process.stderr.decode())
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual({"schema_version": 1, "status": "complete", "dependencies": []}, self.report)
        self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))
        self.assertEqual(2, self.resolve()["profile"]["id"])

    def test_early_metadata_failure_replaces_stale_success_with_failed_record(self):
        self.report_file.write_text(json.dumps({"schema_version": 1, "status": "complete", "dependencies": []}))
        self.responses["message"] = None
        process = self.handler()
        self.assertNotEqual(0, process.returncode)
        self.assertEqual("failed", json.loads(self.report_file.read_text())["status"])

    def test_record_is_optional_for_existing_callers(self):
        process = self.handler("timer", with_record=False)
        self.assertEqual(0, process.returncode, process.stderr.decode())
        self.assertFalse(self.report_file.exists())


class IdentityTests(InputFixture):
    def test_complete_input_record_and_relative_handoff(self):
        result = self.identify()
        inputs = result["inputs"]
        self.assertEqual(1, inputs["schema_version"])
        self.assertEqual(self.request["profile"], inputs["profile"])
        self.assertEqual(self.request["dependencies"], inputs["dependencies"])
        self.assertEqual(IMAGE, inputs["container"])
        self.assertEqual("x86_64-softmmu", inputs["target"])
        self.assertEqual("11.0.0", inputs["sources"]["qemu"]["version"])
        self.assertEqual(["--enable-werror", "--disable-docs"], inputs["configure_args"])
        self.assertEqual(git(self.qemu, "rev-parse", "HEAD^{tree}"), inputs["sources"]["qemu"]["tree"])
        self.assertEqual({}, inputs["sources"]["qemu"]["submodules"])
        path = "infra/builder/compile-qemu.sh"
        self.assertEqual({"mode": "100644", "sha256": hashlib.sha256((path + "\n").encode()).hexdigest()},
                         inputs["recipes"][path])
        self.assertRegex(result["key"], r"^[0-9a-f]{64}$")
        self.assertEqual("qemu/" + result["key"], result["bundle_relative_path"])
        self.assertEqual("qemu/" + result["key"] + "/manifest.json", result["manifest_relative_path"])
        self.assertFalse((self.root / "artifacts").exists())
        self.assertFalse((self.root / "dist").exists())

    def test_json_field_order_and_container_tag_do_not_change_identity(self):
        first = self.identify()
        reordered = dict(reversed(list(self.request.items())))
        reordered["profile"] = dict(reversed(list(self.request["profile"].items())))
        reordered["container_image"] = "evssim:another-tag-for-the-same-image"
        second = self.identify(reordered)
        self.assertEqual(first["key"], second["key"])
        self.assertNotEqual(first["container_reference"], second["container_reference"])

    def test_profile_target_flags_and_dependency_metadata_affect_identity(self):
        original = self.identify()["key"]
        mutations = [
            lambda r: r["profile"].update(id=1),
            lambda r: r["profile"]["hostTests"].update(runContainer="ubuntu-26.04"),
            lambda r: r.update(target="aarch64-softmmu"),
            lambda r: r["configure_args"].append("--disable-tools"),
            lambda r: r["configure_args"].reverse(),
            lambda r: r["build_environment"].update(CFLAGS="-O1"),
            lambda r: r["dependencies"][0].update(patch_set=8),
            lambda r: r["dependencies"][0].update(revision="c" * 40),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(input=index):
                request = copy.deepcopy(self.request)
                mutate(request)
                self.assertNotEqual(original, self.identify(request)["key"])

    def test_immutable_container_identity_and_platform_affect_key(self):
        original = self.identify()["key"]
        for field, value in (("image_id", "sha256:" + "c" * 64),
                             ("architecture", "arm64"), ("variant", "v8"), ("os", "other")):
            with self.subTest(field=field):
                image = dict(IMAGE, **{field: value})
                self.image_mock.return_value = image
                self.assertNotEqual(original, self.identify()["key"])

    def test_changed_qemu_and_authoritative_simulator_revisions_affect_key(self):
        previous = self.identify()["key"]
        for name, repo, path in (("qemu", self.qemu, "source.c"),
                                 ("simulator", self.simulator, "eVSSIM/ssd.c")):
            with self.subTest(source=name):
                write(repo, path, "changed source\n")
                self.request["sources"][name]["revision"] = commit(repo, path)
                current = self.identify()["key"]
                self.assertNotEqual(previous, current)
                previous = current

    def test_changed_recipe_is_recorded(self):
        first = self.identify()
        path = "infra/builder/prepare-qemu-vssim.sh"
        write(self.simulator, path, "changed preparation\n")
        self.request["sources"]["simulator"]["revision"] = commit(self.simulator, path)
        second = self.identify()
        self.assertNotEqual(first["key"], second["key"])
        self.assertNotEqual(first["inputs"]["recipes"][path], second["inputs"]["recipes"][path])

    def test_dependency_order_is_irrelevant_and_cherry_pick_shas_are_retained(self):
        self.request["dependencies"].append({
            "source": "simulator", "url": "https://review.gerrithub.io/c/test/simulator/+/2",
            "patch_set": 13, "revision": "d" * 40,
            "applied_revision": self.request["sources"]["simulator"]["revision"],
            "target_branch": "master",
        })
        first = self.identify()
        self.request["dependencies"].reverse()
        self.assertEqual(first["key"], self.identify()["key"])
        dependency = first["inputs"]["dependencies"][0]
        self.assertNotEqual(dependency["revision"], dependency["applied_revision"])

    def test_no_declared_dependencies_is_valid(self):
        self.request["dependencies"] = []
        self.assertEqual([], self.identify()["inputs"]["dependencies"])

    def test_unapplied_dependency_fails_even_when_object_exists(self):
        tree = git(self.qemu, "rev-parse", "HEAD^{tree}")
        future = subprocess.check_output(
            ["git", "-C", str(self.qemu), "commit-tree", tree, "-p", "HEAD"], input=b"not applied\n"
        ).decode().strip()
        self.request["dependencies"][0]["applied_revision"] = future
        with self.assertRaises(artifact.InputError):
            self.identify()

    def test_unrecorded_tracked_changes_and_wrong_head_fail(self):
        for repo, path in ((self.qemu, "source.c"), (self.simulator, "eVSSIM/ssd.c")):
            with self.subTest(repo=repo):
                original = (repo / path).read_text()
                write(repo, path, "unrecorded change\n")
                with self.assertRaisesRegex(artifact.InputError, "tracked source"):
                    self.identify()
                write(repo, path, original)
        request = copy.deepcopy(self.request)
        request["sources"]["qemu"]["revision"] = "f" * 40
        with self.assertRaisesRegex(artifact.InputError, "HEAD differs"):
            self.identify(request)

    def test_untracked_outputs_are_excluded_from_identity(self):
        first = self.identify()
        write(self.qemu, "build/generated", "generated output\n")
        write(self.simulator, "unrelated-output", "not a source input\n")
        self.assertEqual(first, self.identify())

    def test_gitlink_pin_is_recorded_and_wrong_submodule_checkout_fails(self):
        module = self.root / "module"
        new_repo(module)
        write(module, "module.c", "first module input\n")
        revision = commit(module, "module.c")
        git(self.qemu, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(module), "vendor/module")
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, ".gitmodules", "vendor/module")
        first = self.identify()
        self.assertEqual({"vendor/module": revision}, first["inputs"]["sources"]["qemu"]["submodules"])
        checkout = self.qemu / "vendor/module"
        git(checkout, "config", "user.name", "Artifact test")
        git(checkout, "config", "user.email", "artifact-test@example.invalid")
        write(checkout, "module.c", "next module input\n")
        commit(checkout, "module.c")
        with self.assertRaisesRegex(artifact.InputError, "submodule changes"):
            self.identify()
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, "vendor/module")
        self.assertNotEqual(first["key"], self.identify()["key"])

    def test_missing_or_linked_recipe_is_rejected(self):
        path = "infra/builder/prepare-qemu-vssim.sh"
        (self.simulator / path).unlink()
        self.request["sources"]["simulator"]["revision"] = commit(self.simulator, path)
        with self.assertRaisesRegex(artifact.InputError, "tracked regular file"):
            self.identify()
        os.symlink("compile-qemu.sh", str(self.simulator / path))
        self.request["sources"]["simulator"]["revision"] = commit(self.simulator, path)
        with self.assertRaisesRegex(artifact.InputError, "tracked regular file"):
            self.identify()

    def test_running_helper_must_match_recorded_recipe(self):
        path = artifact.RECIPE_FILES[0]
        write(self.simulator, path, "another helper version\n")
        self.request["sources"]["simulator"]["revision"] = commit(self.simulator, path)
        with self.assertRaisesRegex(artifact.InputError, "helper differs"):
            self.identify()

    def test_qemu_version_is_taken_from_a_tracked_regular_file(self):
        (self.qemu / "VERSION").unlink()
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, "VERSION")
        with self.assertRaisesRegex(artifact.InputError, "VERSION must be a tracked regular"):
            self.identify()
        (self.qemu / "VERSION").symlink_to("source.c")
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, "VERSION")
        with self.assertRaisesRegex(artifact.InputError, "VERSION must be a tracked regular"):
            self.identify()

    def test_invalid_request_fields_are_rejected(self):
        mutations = [
            lambda r: r.update(unrecognized_build_option=True),
            lambda r: r["sources"]["qemu"].update(revision="HEAD"),
            lambda r: r["sources"]["qemu"].update(revision="a" * 40 + "\n"),
            lambda r: r["profile"].update(id=True),
            lambda r: r["profile"].pop("guestTests"),
            lambda r: r["profile"]["qemu"].update(branch="master"),
            lambda r: r.update(configure_args="--enable-werror"),
            lambda r: r["configure_args"].append("--target-list=other"),
            lambda r: r["build_environment"].update({"BAD=NAME": "value"}),
            lambda r: r["dependencies"][0].update(target_branch="master"),
            lambda r: r["dependencies"][0].update(source="kernel"),
            lambda r: r["dependencies"][0].update(patch_set=0),
            lambda r: r["dependencies"].append(copy.deepcopy(r["dependencies"][0])),
            lambda r: r["profile"].update(invalid_number=float("nan")),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(input=index):
                request = copy.deepcopy(self.request)
                mutate(request)
                with self.assertRaises(ValueError):
                    self.identify(request)

    def run_cli(self, text):
        request_path = self.root / "request.json"
        request_path.write_text(text)
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
        env["PATH"] = str(self.root / "bin") + os.pathsep + env["PATH"]
        process = subprocess.Popen(
            [sys.executable, str(HELPER), "--request", str(request_path),
             "--qemu-dir", str(self.qemu), "--simulator-dir", str(self.simulator)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
        )
        stdout, stderr = process.communicate()
        return process.returncode, stdout, stderr

    def test_cli_success_and_failure_exit_contract(self):
        docker = self.root / "bin/docker"
        docker.parent.mkdir()
        docker.write_text("#!" + sys.executable + "\nimport json, sys\n"
                          "assert sys.argv[1:] == " + repr([
                              "image", "inspect", "--format", "{{json .}}", "--", self.request["container_image"]
                          ]) + "\nprint(json.dumps(" + repr({
                              "Id": IMAGE["image_id"], "Os": "linux", "Architecture": "amd64"
                          }) + "))\n")
        docker.chmod(0o755)
        status, stdout, stderr = self.run_cli(json.dumps(self.request))
        self.assertEqual((0, b""), (status, stderr))
        self.assertEqual(self.identify(), json.loads(stdout))
        for text in ('{"sources":{},"sources":{}}', '{"broken":',
                     json.dumps(dict(self.request, container_image="missing-image"))):
            with self.subTest(text=text):
                status, stdout, stderr = self.run_cli(text)
                self.assertEqual(1, status)
                self.assertEqual(b"", stdout)
                self.assertIn(b"ERROR:", stderr)

    def test_cli_dirty_source_returns_failure_without_an_identity(self):
        write(self.qemu, "source.c", "unrecorded change\n")
        status, stdout, stderr = self.run_cli(json.dumps(self.request))
        self.assertEqual(1, status)
        self.assertEqual(b"", stdout)
        self.assertIn(b"tracked source", stderr)


class SavedInputTests(InputFixture):
    def setUp(self):
        super().setUp()
        self.record = self.identify()

    def validate(self, record=None, environment=None):
        return artifact.validate_saved_inputs(record or self.record, str(self.qemu),
                                              str(self.simulator), environment)

    def test_identity_and_manifest_inputs_validate_without_source_changes(self):
        before = {str(repo): git(repo, "rev-parse", "HEAD") for repo in (self.qemu, self.simulator)}
        for record in (self.record, dict(self.record, build="full-clean-passed", files={})):
            self.image_mock.reset_mock()
            result = self.validate(record)
            self.assertEqual(self.record["inputs"], result["inputs"])
            self.assertEqual(self.record["key"], result["key"])
            self.image_mock.assert_called_once_with(IMAGE["image_id"])
        for repo in (self.qemu, self.simulator):
            self.assertEqual(before[str(repo)], git(repo, "rev-parse", "HEAD"))
            self.assertEqual("", git(repo, "status", "--porcelain"))

    def test_rehashed_source_recipe_and_image_metadata_must_match_reality(self):
        mutations = (
            lambda i: i["sources"]["qemu"].update(tree="f" * 40),
            lambda i: i["sources"]["simulator"].update(tree="f" * 40),
            lambda i: i["sources"]["qemu"].update(version="2.12.0"),
            lambda i: i["sources"]["qemu"].update(submodules={"unrecorded": "f" * 40}),
            lambda i: i["recipes"]["infra/builder/compile-qemu.sh"].update(sha256="f" * 64),
            lambda i: i["container"].update(architecture="arm64"),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                record = copy.deepcopy(self.record)
                mutate(record["inputs"])
                record["key"] = artifact.input_key(record["inputs"])
                with self.assertRaisesRegex(artifact.InputError, "differ from the current inputs"):
                    self.validate(record)

    def test_qemu_revision_mismatch_is_rejected(self):
        write(self.qemu, "changed.c", "changed QEMU revision\n")
        commit(self.qemu, "changed.c")
        with self.assertRaisesRegex(artifact.InputError, "source HEAD differs"):
            self.validate()

    def test_simulator_revision_mismatch_is_rejected(self):
        write(self.simulator, "eVSSIM/changed.c", "changed simulator revision\n")
        commit(self.simulator, "eVSSIM/changed.c")
        with self.assertRaisesRegex(artifact.InputError, "source HEAD differs"):
            self.validate()

    def test_dirty_tracked_inputs_in_either_source_are_rejected_and_preserved(self):
        for repo, path in ((self.qemu, "source.c"), (self.simulator, "eVSSIM/ssd.c")):
            with self.subTest(repo=str(repo)):
                original = (repo / path).read_bytes()
                write(repo, path, "uncommitted source change\n")
                with self.assertRaisesRegex(artifact.InputError, "tracked source or submodule changes"):
                    self.validate()
                self.assertEqual("uncommitted source change\n", (repo / path).read_text())
                (repo / path).write_bytes(original)

    def test_saved_dependency_must_still_be_reachable(self):
        record = copy.deepcopy(self.record)
        record["inputs"]["dependencies"][0]["applied_revision"] = "f" * 40
        record["key"] = artifact.input_key(record["inputs"])
        with self.assertRaisesRegex(artifact.InputError, "is not reachable"):
            self.validate(record)

    def test_all_loaded_profile_fields_must_match(self):
        environment = {
            "EVSSIM_VERSIONS_CONFIGURATION_ID": "4",
            "EVSSIM_KERNEL_BRANCH": "master", "EVSSIM_KERNEL_COMPILE_CONTAINER": "ubuntu-14.04",
            "EVSSIM_QEMU_BRANCH": "11.0", "EVSSIM_QEMU_COMPILE_CONTAINER": "ubuntu-26.04",
            "EVSSIM_NVME_CLI_BRANCH": "master", "EVSSIM_NVME_CLI_COMPILE_CONTAINER": "ubuntu-14.04",
            "EVSSIM_HOST_TESTS_COMPILE_CONTAINER": "ubuntu-14.04",
            "EVSSIM_HOST_TESTS_RUN_CONTAINER": "ubuntu-14.04",
            "EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE": "ubuntu-14.04",
            "EVSSIM_GUEST_TESTS_GUEST_VM_BUILD_CONTAINER": "ubuntu-14.04",
            "EVSSIM_GUEST_TESTS_COMPILE_CONTAINER": "ubuntu-14.04",
        }
        self.assertEqual(self.record["key"], self.validate(environment=environment)["key"])
        for field in environment:
            for missing in (False, True):
                with self.subTest(field=field, missing=missing):
                    bad = dict(environment)
                    if missing:
                        bad.pop(field)
                    else:
                        bad[field] = "conflicting value"
                    self.image_mock.reset_mock()
                    with self.assertRaisesRegex(artifact.InputError, field):
                        self.validate(environment=bad)
                    self.image_mock.assert_not_called()

    def test_corrupt_input_key_is_rejected_before_inspection(self):
        record = copy.deepcopy(self.record)
        record["inputs"]["configure_args"].append("--disable-vnc")
        self.image_mock.reset_mock()
        with self.assertRaisesRegex(artifact.InputError, "key/schema"):
            self.validate(record)
        self.image_mock.assert_not_called()

    def test_missing_image_is_an_input_validation_failure(self):
        self.image_mock.side_effect = artifact.InputError("image is unavailable")
        with self.assertRaisesRegex(artifact.InputError, "image is unavailable"):
            self.validate()
        self.assertEqual("", git(self.qemu, "status", "--porcelain"))
        self.assertEqual("", git(self.simulator, "status", "--porcelain"))


class ConfigurationTests(InputFixture):
    def setUp(self):
        super().setUp()
        profiles = json.loads((HELPER.parent / "versions/configs.json").read_text())
        request = copy.deepcopy(self.request)
        request["profile"] = next(profile for profile in profiles["configs"] if profile["id"] == 6)
        request["configure_args"].append("--disable-download")
        self.environment_root = self.root
        self.builder = self.environment_root / "simulator/infra/builder"
        (self.builder / "versions").mkdir(parents=True)
        for name in ("env.sh", "load_config.sh", "qemu-artifact.py", "compile-qemu.sh", "run-ci.sh"):
            shutil.copy2(HELPER.parent / name, self.builder / name)
        for name in ("configs.json", "qemu-build-policy.json"):
            shutil.copy2(HELPER.parent / "versions" / name, self.builder / "versions" / name)
        self.record_file = self.root / "selected-inputs.json"
        self.command_log = self.root / "commands.jsonl"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.real_git = shutil.which("git")
        self.script(self.bin / "git", '''import json, os, subprocess, sys
args = sys.argv[1:]
with open(os.environ['S2_CONFIGURATION_TEST_LOG'], 'a') as out:
    out.write(json.dumps({'type': 'git', 'args': args}) + '\\n')
if args[:2] == ['check-ref-format', '--branch']:
    sys.exit(subprocess.call([os.environ['S2_CONFIGURATION_REAL_GIT']] + args))
if args[:1] == ['--no-optional-locks']:
    sys.exit(subprocess.call([os.environ['S2_CONFIGURATION_REAL_GIT']] + args))
if 'checkout' in args and os.environ.get('S2_CONFIGURATION_DIRTY_AFTER_CHECKOUT'):
    with open(os.environ['S2_CONFIGURATION_DIRTY_AFTER_CHECKOUT'], 'a') as source:
        source.write('changed during checkout\\n')
''')
        self.script(self.bin / "docker", '''import json, os, shutil, sys
args = sys.argv[1:]
if args[:2] == ['image', 'inspect']:
    assert args[-1] == 'sha256:' + 'a' * 64, 'saved input must inspect its immutable image'
    if os.environ.get('S2_CONFIGURATION_REPLACE_RECORD_ON_INSPECT'):
        shutil.copyfile(os.environ['S2_CONFIGURATION_REPLACE_RECORD_ON_INSPECT'], os.environ['EVSSIM_QEMU_INPUT_RECORD'])
    print(json.dumps({'Id': 'sha256:' + 'a' * 64, 'Os': 'linux', 'Architecture': 'amd64'}))
elif args[:1] == ['run']:
    with open(os.environ['S2_CONFIGURATION_TEST_LOG'], 'a') as out:
        out.write(json.dumps({'type': 'saved-container', 'args': args}) + '\\n')
    assert 'sha256:' + 'a' * 64 in args
    if '--version' in args:
        print('QEMU emulator version 11.0.0')
    else:
        assert '-L' in args and '-S' in args, 'a cache hit must not compile'
        assert sys.stdin.buffer.read() == b'quit\\n'
else:
    sys.exit(90)
''')
        self.extra_env = {"S2_CONFIGURATION_TEST_LOG": str(self.command_log),
                          "S2_CONFIGURATION_REAL_GIT": self.real_git}
        (self.builder / "builder.sh").write_text('''#!/bin/bash
printf '{"type":"builder"}\\n' >> "$S2_CONFIGURATION_TEST_LOG"
evssim_validate_version_arguments() { return 0; }
evssim_run_at_folder() {
    python3 -c 'import json,os,sys;open(os.environ["S2_CONFIGURATION_TEST_LOG"],"a").write(json.dumps({"type":"container","args":sys.argv[1:]})+"\\n")' "$@"
}
cd "$EVSSIM_ROOT_PATH"
''')
        self.script(self.builder / "prepare-qemu-vssim.sh", '''import json, os, sys
with open(os.environ['S2_CONFIGURATION_TEST_LOG'], 'a') as out:
    out.write(json.dumps({'type': 'prepare', 'args': sys.argv[1:]}) + '\\n')
''')
        # The compiler preflight now checks real Git sources/recipes. Keep
        # build commands controlled, but commit their fixture files so the
        # source identity is complete and clean rather than bypassing it.
        files = [str(path.relative_to(self.simulator)) for path in self.builder.rglob("*") if path.is_file()]
        request["sources"]["simulator"]["revision"] = commit(self.simulator, *files)
        self.record = self.identify(request)
        self.record_file.write_text(json.dumps(self.record))
        self.cache_record()

    def cache_record(self, record=None):
        record = record or self.record
        folder = self.root / "artifacts/qemu" / record["key"]
        write(folder, "bin/qemu-system-x86_64", "cached executable")
        (folder / "bin/qemu-system-x86_64").chmod(0o755)
        for name in artifact.REQUIRED_FIRMWARE:
            write(folder, "share/qemu/" + name, "firmware")
        for name in artifact.REQUIRED_MONITOR_ASSETS:
            write(folder, "vssim/simulator/www/" + name, "monitor asset")
        manifest = {"schema_version": 1, "key": record["key"], "inputs": record["inputs"],
                    "version": "11.0.0", "executable": "bin/qemu-system-x86_64", "data_dir": "share/qemu",
                    "vssim_www_dir": "vssim/simulator/www", "files": artifact.file_inventory(folder),
                    "build": "full-clean-passed"}
        write(folder, "manifest.json", json.dumps(manifest))

    def script(self, path, body):
        path.write_text("#!" + sys.executable + "\n" + body)
        path.chmod(0o755)

    def shell(self, script, arguments=(), settings=None):
        env = {name: value for name, value in os.environ.items() if not name.startswith("EVSSIM_")}
        env.update(self.extra_env)
        env["PATH"] = str(self.bin) + os.pathsep + env["PATH"]
        env.update(settings or {})
        return subprocess.run(["bash", "-c", script, "configuration-test", *arguments],
                              cwd=self.builder, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def environment(self, settings=None):
        dump = 'import json,os;print(json.dumps({k:v for k,v in os.environ.items() if k.startswith("EVSSIM_")}))'
        result = self.shell('source ./env.sh || exit $?; python3 -c "$1"', [dump], settings)
        if result.returncode:
            return result, None
        return result, json.loads(result.stdout.splitlines()[-1])

    def commands(self):
        return [json.loads(line) for line in self.command_log.read_text().splitlines()] if self.command_log.exists() else []

    def assert_preflight_failed(self, result):
        self.assertNotEqual(0, result.returncode, result.stdout.decode())
        self.assertFalse(any(entry["type"] in ("builder", "container", "saved-container", "prepare") for entry in self.commands()))
        self.assertFalse(any(entry["type"] == "git" and any(action in entry["args"]
                             for action in ("fetch", "checkout", "pull", "reset", "clean", "submodule"))
                             for entry in self.commands()))

    def test_environment_loads_declared_default_and_explicit_profiles(self):
        for profile, branch, container, guest in ((None, "master", "ubuntu-14.04", "ubuntu-14.04"),
                                                 (1, "11.0", "ubuntu-26.04", "ubuntu-26.04"),
                                                 (5, "master", "ubuntu-14.04", "ubuntu-14.04"),
                                                 (6, "11.0", "ubuntu-26.04", "ubuntu-14.04")):
            with self.subTest(profile=profile):
                result, env = self.environment({} if profile is None else {"EVSSIM_VERSIONS_CONFIGURATION_ID": str(profile)})
                self.assertEqual(0, result.returncode, result.stderr.decode())
                self.assertEqual(str(profile or 5), env["EVSSIM_VERSIONS_CONFIGURATION_ID"])
                self.assertEqual(branch, env["EVSSIM_QEMU_BRANCH"])
                self.assertEqual(container, env["EVSSIM_QEMU_COMPILE_CONTAINER"])
                self.assertEqual(guest, env["EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"])

    def test_artifact_policy_does_not_change_existing_environment_default(self):
        policy = self.builder / "versions/qemu-build-policy.json"
        changed = json.loads(policy.read_text())
        changed["default_profile"] = 4
        policy.write_text(json.dumps(changed))
        result, env = self.environment()
        self.assertEqual(0, result.returncode, result.stderr.decode())
        self.assertEqual("5", env["EVSSIM_VERSIONS_CONFIGURATION_ID"])

    def test_environment_does_not_implicitly_load_an_artifact_record(self):
        # Saved records belong to the artifact helper. Sourcing the shared
        # environment retains its original config-file/default behavior.
        result, env = self.environment({"EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)})
        self.assertEqual(0, result.returncode, result.stderr.decode())
        self.assertEqual("5", env["EVSSIM_VERSIONS_CONFIGURATION_ID"])
        self.assertEqual("master", env["EVSSIM_QEMU_BRANCH"])
        self.assertEqual("ubuntu-14.04", env["EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"])
        self.assertEqual([], self.commands())

    def test_unknown_configuration_id_stops_environment_loading(self):
        result, _ = self.environment({"EVSSIM_VERSIONS_CONFIGURATION_ID": "99"})
        self.assertNotEqual(0, result.returncode)
        self.assertEqual([], self.commands())

    def test_compile_uses_selected_branch_even_with_the_same_container(self):
        path = self.builder / "versions/configs.json"
        changed = json.loads(path.read_text())
        next(profile for profile in changed["configs"] if profile["id"] == 6)["qemu"]["branch"] = "another-target"
        path.write_text(json.dumps(changed))
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6"})
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        checkouts = [entry["args"][-1] for entry in self.commands()
                     if entry["type"] == "git" and "checkout" in entry["args"]]
        self.assertEqual(["another-target"], checkouts)
        self.assertTrue(any(entry["type"] == "prepare" for entry in self.commands()))

    def test_compile_container_conflict_stops_before_builder_git_and_cleanup(self):
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"', ["ubuntu-26.04"])
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"conflicts", result.stderr)
        self.assertEqual([], self.commands())

    def test_saved_revision_is_frozen_and_mismatch_stops_before_cleanup_or_build(self):
        settings = {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)}
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], settings)
        self.assertEqual(0, result.returncode, result.stderr.decode())
        self.assertFalse(any(entry["type"] == "git" and "pull" in entry["args"] for entry in self.commands()))
        self.command_log.unlink()
        write(self.qemu, "new-source.c", "changed source revision\n")
        commit(self.qemu, "new-source.c")
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], settings)
        self.assertNotEqual(0, result.returncode)
        self.assertIn(b"source HEAD differs", result.stderr)
        self.assertFalse(any(entry["type"] in ("container", "prepare") for entry in self.commands()))
        self.assertFalse(any(entry["type"] == "git" and any(action in entry["args"] for action in ("reset", "clean"))
                             for entry in self.commands()))
        self.assertFalse(any(entry["type"] == "builder" for entry in self.commands()))

    def test_saved_simulator_revision_is_checked_before_builder_setup(self):
        write(self.simulator, "eVSSIM/new-source.c", "changed simulator revision\n")
        commit(self.simulator, "eVSSIM/new-source.c")
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)})
        self.assert_preflight_failed(result)
        self.assertIn(b"source HEAD differs", result.stderr)

    def test_saved_record_rejects_dirty_sources_before_builder_setup(self):
        for repo, path in ((self.qemu, "source.c"), (self.simulator, "eVSSIM/ssd.c")):
            with self.subTest(repo=str(repo)):
                original = (repo / path).read_bytes()
                write(repo, path, "preserve this uncommitted change\n")
                result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                                    ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)})
                self.assert_preflight_failed(result)
                self.assertIn(b"tracked source or submodule changes", result.stderr)
                self.assertEqual("preserve this uncommitted change\n", (repo / path).read_text())
                (repo / path).write_bytes(original)

    def test_saved_record_rejects_component_overrides_after_environment_loading(self):
        for field in ("EVSSIM_KERNEL_BRANCH", "EVSSIM_NVME_CLI_COMPILE_CONTAINER",
                      "EVSSIM_HOST_TESTS_RUN_CONTAINER", "EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE"):
            with self.subTest(field=field):
                result = self.shell('source ./env.sh || exit $?; export "$2=wrong"; bash ./compile-qemu.sh "$1"',
                                    ["ubuntu-26.04", field], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)})
                self.assert_preflight_failed(result)
                self.assertIn(field.encode(), result.stderr)

    def test_saved_build_does_not_checkout_or_clean_development_sources(self):
        source = self.simulator / "eVSSIM/ssd.c"
        previous = source.read_text()
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file),
                             "S2_CONFIGURATION_DIRTY_AFTER_CHECKOUT": str(source)})
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        self.assertEqual("hit", json.loads(result.stdout.splitlines()[-1])["cache"])
        self.assertFalse(any(entry["type"] in ("builder", "container", "prepare") for entry in self.commands()))
        self.assertFalse(any(entry["type"] == "git" and any(action in entry["args"] for action in ("fetch", "checkout", "pull", "reset", "clean"))
                             for entry in self.commands()))
        self.assertEqual(previous, source.read_text())

    def test_validate_inputs_cli_is_read_only_and_rejects_build_options(self):
        args = [str(self.record_file), str(self.qemu), str(self.simulator)]
        command = 'python3 ./qemu-artifact.py --validate-inputs "$1" --qemu-dir "$2" --simulator-dir "$3"'
        result = self.shell(command, args)
        self.assertEqual(0, result.returncode, result.stderr.decode())
        self.assertEqual(self.record["inputs"], json.loads(result.stdout)["inputs"])
        for option in ('--build-dir "$4"', '--profile-id 5', '--selection-policy "$4"'):
            with self.subTest(option=option):
                result = self.shell(command + " " + option, args + [str(self.root / "must-not-build")])
                self.assertNotEqual(0, result.returncode)
                self.assertEqual(b"", result.stdout)
                self.assertFalse((self.root / "must-not-build").exists())
        self.assertEqual("", git(self.qemu, "status", "--porcelain"))
        self.assertEqual("", git(self.simulator, "status", "--porcelain"))

    def test_saved_build_captures_record_once_before_execution(self):
        changed = copy.deepcopy(self.record)
        changed["inputs"]["configure_args"].append("--disable-vnc")
        changed["key"] = artifact.input_key(changed["inputs"])
        replacement = self.root / "replacement-inputs.json"
        replacement.write_text(json.dumps(changed))
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file),
                             "S2_CONFIGURATION_REPLACE_RECORD_ON_INSPECT": str(replacement)})
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        selected = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(self.record["key"], selected["key"])
        self.assertNotEqual(changed["key"], selected["key"])
        self.assertEqual(changed["key"], json.loads(self.record_file.read_text())["key"])
        self.assertFalse(any(entry["type"] in ("builder", "container", "prepare") for entry in self.commands()))
        self.assertFalse(any(entry["type"] == "git" and any(action in entry["args"] for action in ("fetch", "checkout", "pull", "reset", "clean"))
                             for entry in self.commands()))

    def test_saved_compile_preserves_image_and_environment_on_cache_hit(self):
        record = copy.deepcopy(self.record)
        literal = "spaces; $(touch MUST_NOT_EXIST) `false`"
        record["inputs"]["build_environment"] = {"CFLAGS": literal}
        record["inputs"]["configure_args"].append("--extra-cflags=" + literal)
        record["key"] = artifact.input_key(record["inputs"])
        self.record_file.write_text(json.dumps(record))
        self.cache_record(record)
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-26.04"], {"EVSSIM_VERSIONS_CONFIGURATION_ID": "6", "EVSSIM_QEMU_INPUT_RECORD": str(self.record_file)})
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        selected = json.loads(result.stdout.splitlines()[-1])
        self.assertEqual(record["key"], selected["key"])
        self.assertEqual("hit", selected["cache"])
        probes = [entry["args"] for entry in self.commands() if entry["type"] == "saved-container"]
        self.assertTrue(probes)
        for args in probes:
            self.assertIn(IMAGE["image_id"], args)
            self.assertIn("CFLAGS=" + literal, args)
            self.assertNotIn(self.request["container_image"], args)
            self.assertNotIn("--privileged", args)
        self.assertFalse((self.builder / "MUST_NOT_EXIST").exists())

    def test_legacy_compile_retains_master_and_existing_tool_builds(self):
        result = self.shell('source ./env.sh || exit $?; bash ./compile-qemu.sh "$1"', ["ubuntu-14.04"])
        self.assertEqual(0, result.returncode, result.stderr.decode())
        self.assertTrue(any(entry["type"] == "git" and entry["args"][-2:] == ["checkout", "master"]
                            for entry in self.commands()))
        self.assertTrue(any(entry["type"] == "container" and "open-osd" in entry["args"]
                            for entry in self.commands()))

    def test_saved_legacy_version_wrapper_cannot_silently_skip_tool_builds(self):
        result = self.shell('source ./env.sh || exit $?; export EVSSIM_QEMU_INPUT_RECORD="$2"; bash ./compile-qemu.sh "$1"',
                            ["ubuntu-14.04", str(self.record_file)])
        self.assert_preflight_failed(result)
        self.assertIn(b"must also build exofs tools", result.stderr)
        self.assertIn(b"qemu-artifact.py explicitly", result.stderr)

    def prepare_ci_workflow(self, stub_compiler=False):
        body = '''import json, os, pathlib, sys
with open(os.environ['S2_CONFIGURATION_TEST_LOG'], 'a') as out:
    out.write(json.dumps({'type': 'stage', 'name': pathlib.Path(sys.argv[0]).name,
                         'args': sys.argv[1:], 'id': os.environ.get('EVSSIM_VERSIONS_CONFIGURATION_ID'),
                         'branch': os.environ.get('EVSSIM_QEMU_BRANCH'),
                         'container': os.environ.get('EVSSIM_QEMU_COMPILE_CONTAINER'),
                         'guest': os.environ.get('EVSSIM_GUEST_TESTS_GUEST_VM_IMAGE')}) + '\\n')
'''
        names = ["check_tools.sh", "build-docker-image.sh", "build-qemu-image.sh", "compile-kernel.sh",
                 "compile-host-tests.sh", "compile-guest-tests.sh", "docker-run-sanity.sh",
                 "docker-test-host.sh", "docker-test-host-elk.sh", "docker-test-guest.sh", "docker-test-exofs.sh"]
        if stub_compiler:
            names.append("compile-qemu.sh")
        for name in names:
            self.script(self.builder / name, body)
        self.script(self.bin / "tox", body)
        elk = self.environment_root / "simulator/infra/ELK"
        elk.mkdir()
        for name in ("install_and_start_elk.sh", "elk_cleanup.sh"):
            self.script(elk / name, body)

    def assert_ci_runtime_stages(self, stages, profile, host_container):
        expected = {
            "build-qemu-image.sh": ["ubuntu-14.04", "ubuntu-14.04"],
            "compile-kernel.sh": ["ubuntu-14.04"],
            "compile-host-tests.sh": [host_container],
            "compile-guest-tests.sh": ["ubuntu-14.04"],
            "docker-run-sanity.sh": ["ubuntu-14.04"],
            "docker-test-host.sh": [host_container],
            "docker-test-host-elk.sh": [host_container],
            "docker-test-guest.sh": ["ubuntu-14.04"],
            "docker-test-exofs.sh": ["ubuntu-14.04"],
        }
        for name, arguments in expected.items():
            with self.subTest(stage=name):
                stage = next(entry for entry in stages if entry["name"] == name)
                self.assertEqual(arguments, stage["args"])
                self.assertEqual((profile, "master", "ubuntu-14.04", "ubuntu-14.04"),
                                 (stage["id"], stage["branch"], stage["container"], stage["guest"]))

    def test_existing_ci_build_checks_preserve_legacy_compatibility_runtime(self):
        self.prepare_ci_workflow(stub_compiler=True)
        result = self.shell('bash ./run-ci.sh')
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        stages = [entry for entry in self.commands() if entry["type"] == "stage"]
        builds = [entry for entry in stages if entry["name"] == "compile-qemu.sh"]
        self.assertEqual([("6", "11.0", ["ubuntu-26.04"]), ("5", "master", ["ubuntu-14.04"])],
                         [(entry["id"], entry["branch"], entry["args"]) for entry in builds])
        self.assert_ci_runtime_stages(stages, "5", "ubuntu-26.04")

    def test_ci_uses_declared_guest_settings_without_profile_five_overrides(self):
        self.prepare_ci_workflow(stub_compiler=True)
        path = self.builder / "versions/configs.json"
        profiles = json.loads(path.read_text())
        legacy = next(profile for profile in profiles["configs"] if profile["id"] == 5)
        # A declared change must reach the caller; hidden 14.04 assignments
        # would make these assertions fail even though the default run passes.
        legacy["guestTests"] = {field: "ubuntu-26.04" for field in legacy["guestTests"]}
        path.write_text(json.dumps(profiles))
        result = self.shell('bash ./run-ci.sh')
        self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        stages = [entry for entry in self.commands() if entry["type"] == "stage"]
        image = next(entry for entry in stages if entry["name"] == "build-qemu-image.sh")
        tools = next(entry for entry in stages if entry["name"] == "compile-guest-tests.sh")
        self.assertEqual(["ubuntu-26.04", "ubuntu-26.04"], image["args"])
        self.assertEqual(["ubuntu-26.04"], tools["args"])
        for stage in stages:
            if stage["name"] != "compile-qemu.sh":
                self.assertEqual("ubuntu-26.04", stage["guest"])

    def test_ci_real_compiler_keeps_legacy_build_and_test_containers_aligned(self):
        self.prepare_ci_workflow()
        cases = [(None, "5", "ubuntu-26.04"), ("5", "5", "ubuntu-26.04"), ("2", "2", "ubuntu-14.04")]
        for selected, expected_profile, host_container in cases:
            with self.subTest(profile=selected):
                self.command_log.unlink(missing_ok=True)
                settings = {} if selected is None else {"EVSSIM_VERSIONS_CONFIGURATION_ID": selected}
                result = self.shell('bash ./run-ci.sh', settings=settings)
                self.assertEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
                commands = self.commands()
                checkouts = [(index, entry["args"][-1]) for index, entry in enumerate(commands)
                             if entry["type"] == "git" and "checkout" in entry["args"]]
                self.assertEqual(["11.0", "master"], [branch for _, branch in checkouts])
                builds = [(index, entry) for index, entry in enumerate(commands)
                          if entry["type"] == "container" and "bear" in entry["args"]]
                self.assertEqual(2, len(builds))
                modern, legacy = builds
                self.assertEqual("ubuntu-26.04", modern[1]["args"][0])
                self.assertIn("ninja", modern[1]["args"])
                self.assertEqual("ubuntu-14.04", legacy[1]["args"][0])
                self.assertIn("make", legacy[1]["args"])
                self.assertLess(checkouts[0][0], modern[0])
                self.assertLess(modern[0], checkouts[1][0])
                self.assertLess(checkouts[1][0], legacy[0])
                host_build = next(index for index, entry in enumerate(commands)
                                  if entry["type"] == "stage" and entry["name"] == "compile-host-tests.sh")
                self.assertLess(legacy[0], host_build)
                stages = [entry for entry in commands if entry["type"] == "stage"]
                self.assert_ci_runtime_stages(stages, expected_profile, host_container)

    def assert_ci_rejected_before_stages(self, result, diagnostic):
        self.assertNotEqual(0, result.returncode, result.stdout.decode() + result.stderr.decode())
        self.assertIn(diagnostic, result.stderr)
        self.assertEqual([], self.commands())
        self.assertFalse((self.environment_root / "logs").exists())
        self.assertFalse((self.environment_root / "dist").exists())

    def test_ci_rejects_modern_runtime_profiles_before_any_stage(self):
        self.prepare_ci_workflow()
        for profile in ("1", "4", "6"):
            with self.subTest(profile=profile):
                result = self.shell('bash ./run-ci.sh', settings={"EVSSIM_VERSIONS_CONFIGURATION_ID": profile})
                self.assert_ci_rejected_before_stages(result, b"currently tests legacy QEMU")

    def test_ci_rejects_saved_records_before_reading_or_running_stages(self):
        self.prepare_ci_workflow()
        before = self.record_file.read_bytes()
        for path in (self.record_file, self.root / "missing-inputs.json"):
            with self.subTest(record=path.name):
                result = self.shell('bash ./run-ci.sh', settings={"EVSSIM_QEMU_INPUT_RECORD": str(path)})
                self.assert_ci_rejected_before_stages(result, b"does not consume saved QEMU input records")
                self.assertEqual(before, self.record_file.read_bytes())

    def test_ci_rejects_mismatched_qemu_branch_or_container_in_legacy_profile(self):
        self.prepare_ci_workflow()
        path = self.builder / "versions/configs.json"
        original = json.loads(path.read_text())
        for branch, container in (("11.0", "ubuntu-14.04"), ("master", "ubuntu-26.04")):
            with self.subTest(branch=branch, container=container):
                changed = copy.deepcopy(original)
                profile = next(profile for profile in changed["configs"] if profile["id"] == 5)
                profile["qemu"] = {"branch": branch, "compileContainer": container}
                path.write_text(json.dumps(changed))
                result = self.shell('bash ./run-ci.sh', settings={"EVSSIM_VERSIONS_CONFIGURATION_ID": "5"})
                self.assert_ci_rejected_before_stages(result, b"currently tests legacy QEMU")


class BuildTests(InputFixture):
    def setUp(self):
        super().setUp()
        self.request["configure_args"].append("--disable-download")
        self.cache = self.root / "source-cache"
        self.cache.mkdir()
        self.pins = {}
        for name in artifact.SUBPROJECTS:
            repo = self.cache / name
            new_repo(repo)
            write(repo, "source.c", name + "\n")
            pin = commit(repo, "source.c")
            self.pins[name] = pin
            # A source cache may have outputs or edits; only the pinned Git
            # objects are exported, never these working-tree files.
            write(repo, "source.c", "dirty cached checkout\n")
            write(repo, "cached.o", "must not copy\n")
            write(self.qemu, "subprojects/" + name + ".wrap",
                  "[wrap-git]\nrevision = " + pin + "\npatch_directory = " + name + "\n")
            write(self.qemu, "subprojects/packagefiles/" + name + "/meson.build", "overlay\n")
        write(self.qemu, "pc-bios/bios.bin", "tracked firmware\n")
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, "subprojects", "pc-bios")
        self.destination = self.root / "job"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        docker = self.bin / "docker"
        docker.write_text("#!" + sys.executable + "\n" + '''
import json, pathlib, sys
args = sys.argv[1:]
if args[:2] == ['image', 'inspect']:
    print(json.dumps({'Id': 'sha256:' + 'a' * 64, 'Os': 'linux', 'Architecture': 'amd64'}))
    sys.exit(0)
assert args[0] == 'run' and '--privileged' not in args
assert args[args.index('--entrypoint') + 1] == '/usr/bin/env'
assert args[args.index('--workdir') + 1] == '/code/qemu'
root = pathlib.Path(args[args.index('--volume') + 1].removesuffix(':/code'))
index = next(i for i, arg in enumerate(args) if arg.startswith('sha256:'))
command = args[index + 1:]
env = dict(args[i + 1].split('=', 1) for i, arg in enumerate(args) if arg == '--env')
stage = {'bash': 'prepare', './configure': 'configure', 'ninja': 'build',
         './build/qemu-system-x86_64': 'version'}[command[0]]
print(stage + ' output', flush=True)
if env.get('FAIL_STAGE') == stage:
    sys.exit(7)
qemu = root / 'qemu'
if stage == 'configure':
    assert '--enable-werror' in command and '--disable-download' in command
    assert not (qemu / 'build').exists()
    (qemu / 'build').mkdir()
    (qemu / 'build/meson-info').mkdir()
    (qemu / 'build/meson-info/intro-buildoptions.json').write_text(
        json.dumps([{'name': 'werror', 'value': True}]))
elif stage == 'build':
    assert (qemu / 'build').is_dir()
    (qemu / 'build/qemu-system-x86_64').write_text('built executable')
elif stage == 'version':
    assert (qemu / 'build/qemu-system-x86_64').is_file()
    print('QEMU emulator version 11.0.0')
''')
        docker.chmod(0o755)
        self.path = mock.patch.dict(os.environ, {"PATH": str(self.bin) + os.pathsep + os.environ["PATH"]})
        self.path.start()
        self.addCleanup(self.path.stop)

    def build(self, identity=None):
        return artifact.build(identity or self.identify(), str(self.qemu), str(self.simulator),
                              self.destination, 2, self.cache)

    def saved_cli(self, record, extra=()):
        path = self.root / "saved-inputs.json"
        path.write_text(json.dumps(record))
        args = [str(HELPER), "--saved-inputs", str(path), "--qemu-dir", str(self.qemu),
                "--simulator-dir", str(self.simulator), "--build-dir", str(self.destination),
                "--subproject-cache", str(self.cache), "--jobs", "2", *extra]
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, "argv", args), mock.patch.object(sys, "stdout", stdout), \
                mock.patch.object(sys, "stderr", stderr):
            status = artifact.main()
        return status, stdout.getvalue(), stderr.getvalue()

    def test_saved_cli_build_uses_exact_recorded_recipe(self):
        literal = "spaces; $(touch MUST_NOT_EXIST) `false`"
        self.request["build_environment"] = {"CFLAGS": literal, "LDFLAGS": "-L/recorded library"}
        self.request["configure_args"].append("--extra-cflags=" + literal)
        record = self.identify()
        self.destination = self.root / "new-log-parent/jobs/recorded-build"
        status, stdout, stderr = self.saved_cli(record)
        self.assertEqual(0, status, stderr)
        result = json.loads(stdout)
        self.assertEqual(record["inputs"], result["identity"]["inputs"])
        for command in result["commands"]:
            args = command["argv"]
            self.assertIn(IMAGE["image_id"], args)
            self.assertNotIn(self.request["container_image"], args)
            self.assertIn("CFLAGS=" + literal, args)
            self.assertIn("LDFLAGS=-L/recorded library", args)
        configure = result["commands"][1]["argv"]
        index = configure.index(IMAGE["image_id"]) + 1
        self.assertEqual(["./configure", "--target-list=" + record["inputs"]["target"]] +
                         record["inputs"]["configure_args"], configure[index:])
        self.assertFalse((self.root / "MUST_NOT_EXIST").exists())

    def test_saved_cli_rejects_selection_overrides_and_unsupported_target(self):
        record = self.identify()
        for extra in (("--profile-id", "1"), ("--simulator-branch", "another-branch"),
                      ("--selection-policy", "unused.json")):
            with self.subTest(extra=extra):
                status, stdout, stderr = self.saved_cli(record, extra)
                self.assertEqual(1, status)
                self.assertEqual("", stdout)
                self.assertIn("overrides are not allowed", stderr)
                self.assertFalse(self.destination.exists())
        record["inputs"]["target"] = "aarch64-softmmu"
        record["key"] = artifact.input_key(record["inputs"])
        status, stdout, stderr = self.saved_cli(record)
        self.assertEqual(1, status)
        self.assertEqual("", stdout)
        self.assertIn("currently supports", stderr)
        self.assertFalse(self.destination.exists())

    def test_saved_cli_exports_captured_commits_if_worktree_changes_during_inspection(self):
        record = self.identify()
        original = (self.qemu / "source.c").read_text()
        def inspect(reference):
            write(self.qemu, "source.c", "later uncommitted source change\n")
            return copy.deepcopy(IMAGE)
        self.image_mock.side_effect = inspect
        status, stdout, stderr = self.saved_cli(record)
        self.assertEqual(0, status, stderr)
        result = json.loads(stdout)
        self.assertEqual(record["key"], result["identity"]["key"])
        self.assertEqual(original, (Path(result["qemu_dir"]) / "source.c").read_text())
        self.assertEqual("later uncommitted source change\n", (self.qemu / "source.c").read_text())

    def test_saved_cli_propagates_build_failure_without_success_output(self):
        self.request["build_environment"] = {"FAIL_STAGE": "build"}
        status, stdout, stderr = self.saved_cli(self.identify())
        self.assertEqual(1, status)
        self.assertEqual("", stdout)
        self.assertIn("exit 7", stderr)
        self.assertFalse((self.destination / "workspace").exists())
        self.assertFalse((self.destination / "build-result.json").exists())
        self.assertTrue((self.destination / "logs/build.log").is_file())

    def test_saved_cli_rejects_stale_simulator_before_build(self):
        record = self.identify()
        write(self.simulator, "eVSSIM/new-source.c", "changed simulator revision\n")
        commit(self.simulator, "eVSSIM/new-source.c")
        self.image_mock.reset_mock()
        status, stdout, stderr = self.saved_cli(record)
        self.assertEqual(1, status)
        self.assertEqual("", stdout)
        self.assertIn("source HEAD differs", stderr)
        self.assertFalse(self.destination.exists())
        self.image_mock.assert_not_called()

    def test_success_exports_committed_inputs_and_retains_clean_build(self):
        write(self.qemu, "build/stale.o", "old build output\n")
        write(self.simulator, "untracked.c", "unrecorded\n")
        result = self.build()
        qemu = Path(result["qemu_dir"])
        self.assertEqual("built", result["status"])
        self.assertTrue((qemu / "build/qemu-system-x86_64").is_file())
        self.assertFalse((qemu / "build/stale.o").exists())
        self.assertFalse((qemu.parent / "simulator/untracked.c").exists())
        self.assertEqual("tracked firmware\n", (qemu / "pc-bios/bios.bin").read_text())
        self.assertEqual(self.pins, result["subprojects"])
        for name in artifact.SUBPROJECTS:
            project = qemu / "subprojects" / name
            self.assertEqual(name + "\n", (project / "source.c").read_text())
            self.assertEqual("overlay\n", (project / "meson.build").read_text())
            self.assertFalse((project / "cached.o").exists())
            self.assertFalse((project / ".git").exists())
        commands = result["commands"]
        self.assertEqual(4, len(commands))
        for command in commands:
            self.assertEqual(0, command["exit_status"])
            self.assertIn(IMAGE["image_id"], command["argv"])
            self.assertNotIn(self.request["container_image"], command["argv"])
        self.assertEqual(result, json.loads((self.destination / "build-result.json").read_text()))
        self.assertEqual(commands, json.loads((self.destination / "logs/commands.json").read_text()))
        self.assertFalse((self.root / "artifacts").exists())
        self.assertEqual("old build output\n", (self.qemu / "build/stale.o").read_text())

    def test_each_subprocess_failure_stops_and_cleans_workspace_but_keeps_logs(self):
        for index, stage in enumerate(("prepare", "configure", "build", "version")):
            with self.subTest(stage=stage):
                self.destination = self.root / ("failed-" + stage)
                self.request["build_environment"] = {"FAIL_STAGE": stage}
                with self.assertRaisesRegex(artifact.InputError, "exit 7"):
                    self.build()
                self.assertFalse((self.destination / "workspace").exists())
                self.assertFalse((self.destination / "build-result.json").exists())
                self.assertIn(stage + " output", (self.destination / "logs" / (stage + ".log")).read_text())
                commands = json.loads((self.destination / "logs/commands.json").read_text())
                self.assertEqual(index + 1, len(commands))
                self.assertEqual(7, commands[-1]["exit_status"])

    def test_export_and_missing_pin_fail_before_container_launch(self):
        for stage in ("export", "pin"):
            with self.subTest(stage=stage):
                self.destination = self.root / ("failed-" + stage)
                identity = self.identify()
                if stage == "export":
                    identity["inputs"]["sources"]["qemu"]["revision"] = "f" * 40
                else:
                    (self.cache / artifact.SUBPROJECTS[0]).rename(self.cache / "unavailable")
                with self.assertRaises(artifact.InputError):
                    self.build(identity)
                self.assertFalse((self.destination / "workspace").exists())
                self.assertFalse((self.destination / "build-result.json").exists())
                self.assertEqual([], json.loads((self.destination / "logs/commands.json").read_text()))

    def test_existing_destination_is_never_reused_or_removed(self):
        self.destination.mkdir()
        write(self.destination, "keep", "existing job\n")
        with self.assertRaises(FileExistsError):
            self.build()
        self.assertEqual("existing job\n", (self.destination / "keep").read_text())

    def test_compile_entrypoint_runs_without_builder_environment_and_reports_failure(self):
        for stage in (None, "build"):
            with self.subTest(stage=stage):
                request = copy.deepcopy(self.request)
                request["build_environment"] = {"FAIL_STAGE": stage} if stage else {}
                request_path = self.root / "request.json"
                request_path.write_text(json.dumps(request))
                destination = self.root / (stage or "success")
                process = subprocess.run([
                    "bash", str(HELPER.parent / "compile-qemu.sh"), "--request", str(request_path),
                    "--qemu-dir", str(self.qemu), "--simulator-dir", str(self.simulator),
                    "--build-dir", str(destination), "--subproject-cache", str(self.cache),
                ], cwd=str(self.root), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if stage:
                    self.assertEqual(1, process.returncode)
                    self.assertEqual(b"", process.stdout)
                    self.assertIn(b"exit 7", process.stderr)
                    self.assertFalse((destination / "workspace").exists())
                else:
                    self.assertEqual(0, process.returncode, process.stderr.decode())
                    self.assertEqual("built", json.loads(process.stdout)["status"])

    def test_container_launch_error_also_cleans_workspace(self):
        identity = self.identify()
        with mock.patch.object(artifact.subprocess, "run", side_effect=OSError("cannot launch")):
            # Exercise run_logged through build, after the independent Git
            # exports and source-cache preparation have succeeded.
            with mock.patch.object(artifact, "export_tree"), mock.patch.object(artifact, "prepare_subprojects"):
                with self.assertRaisesRegex(OSError, "cannot launch"):
                    self.build(identity)
        self.assertFalse((self.destination / "workspace").exists())
        self.assertFalse((self.destination / "build-result.json").exists())

    def test_guards_reject_unsupported_build_and_warning_or_download_overrides(self):
        for flags in (["--disable-download"], ["--enable-werror"],
                      self.request["configure_args"] + ["--disable-werror"],
                      self.request["configure_args"] + ["-Dwerror=false"],
                      self.request["configure_args"] + ["--enable-download"]):
            with self.subTest(flags=flags):
                identity = self.identify()
                identity["inputs"]["configure_args"] = flags
                with self.assertRaisesRegex(artifact.InputError, "requires"):
                    self.build(identity)
                self.assertFalse(self.destination.exists())
        identity = self.identify()
        identity["inputs"]["target"] = "aarch64-softmmu"
        with self.assertRaisesRegex(artifact.InputError, "currently supports"):
            self.build(identity)

    def test_effective_werror_must_be_true_before_compilation(self):
        normal = artifact.run_logged
        for options in ([], [{"name": "werror", "value": False}]):
            with self.subTest(options=options):
                self.destination = self.root / ("missing-werror" if not options else "disabled-werror")
                def configured(argv, log, commands, **kwargs):
                    normal(argv, log, commands, **kwargs)
                    if log.stem == "configure":
                        write(self.destination / "workspace/qemu",
                              "build/meson-info/intro-buildoptions.json", json.dumps(options))
                with mock.patch.object(artifact, "run_logged", side_effect=configured):
                    with self.assertRaisesRegex(artifact.InputError, "werror"):
                        self.build()
                self.assertFalse((self.destination / "logs/build.log").exists())
                self.assertFalse((self.destination / "workspace").exists())

    def test_environment_and_arguments_remain_literal(self):
        literal = "spaces; $(touch SHOULD_NOT_EXIST) `false`"
        self.request["build_environment"] = {"CFLAGS": literal}
        self.request["configure_args"].append("--extra-cflags=" + literal)
        result = self.build()
        args = result["commands"][1]["argv"]
        self.assertIn("CFLAGS=" + literal, args)
        self.assertIn("--extra-cflags=" + literal, args)
        self.assertFalse((self.root / "SHOULD_NOT_EXIST").exists())


class LegacyBuildTests(InputFixture):
    def setUp(self):
        super().setUp()
        write(self.qemu, "VERSION", "2.12.50\n")
        write(self.qemu, "configure", '''#!/bin/sh
# Configure optional vssim links
if test "$vssim" = "yes" ; then
    QEMU_CFLAGS="-I/usr/include/json-c/ -D__STDC_VERSION__=0 $QEMU_CFLAGS"
    LIBS="-ljson-c $LIBS"
    vssim_source="$source_path/../simulator/eVSSIM"
    vssim_target="$source_path/hw/vssim/simulator"
    symlink "$vssim_source/FTL_SOURCE/PAGE_MAP/ftl.c" "$vssim_target/ftl.c"
    symlink "$vssim_source/osc-osd" "$vssim_target/osc-osd"
    symlink "$vssim_source/MONITOR/SSD_MONITOR_PM/ssd_monitor_p" "$vssim_target/ssd_monitor"
    make -C "$vssim_target/osc-osd/osd-target" osd-schema.c
fi
''')
        (self.qemu / "configure").chmod(0o755)
        write(self.qemu, "hw/vssim/Makefile.objs", '''common-obj-$(CONFIG_VSSIM) += simulator/ftl.o
common-obj-$(CONFIG_VSSIM) += simulator/osc-osd/osd-target/osd-schema.o
# common-obj-$(CONFIG_VSSIM) += simulator/obsolete_stub.o
''')
        self.pins = {}
        modules = []
        for path in artifact.LEGACY_SUBMODULES:
            repo = self.qemu / path
            repo.parent.mkdir(parents=True, exist_ok=True)
            new_repo(repo)
            write(repo, "source.c", path + " pinned input\n")
            self.pins[path] = commit(repo, "source.c")
            modules.append('[submodule "' + path + '"]\n path = ' + path + '\n url = https://example.invalid/' + path + '\n')
        write(self.qemu, ".gitmodules", "".join(modules))
        revision = commit(self.qemu, "VERSION", "configure", "hw", ".gitmodules", *artifact.LEGACY_SUBMODULES)
        self.request["sources"]["qemu"] = {"revision": revision, "target_branch": "master"}
        self.request["profile"] = json.loads((HELPER.parent / "versions/configs.json").read_text())["configs"][1]
        self.request["dependencies"] = []
        recipe = json.loads((HELPER.parent / "versions/qemu-build-policy.json").read_text())["builds"]["ubuntu-14.04"]
        self.request.update(copy.deepcopy(recipe))
        self.destination = self.root / "legacy-job"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        docker = self.bin / "docker"
        docker.write_text("#!" + sys.executable + "\n" + '''import json, os, pathlib, sys
args = sys.argv[1:]
assert args[0] == 'run' and '--privileged' not in args
assert args[args.index('--workdir') + 1] == '/code/qemu'
assert args[args.index('--entrypoint') + 1] == '/usr/bin/env'
root = pathlib.Path(args[args.index('--volume') + 1].removesuffix(':/code'))
index = args.index('sha256:' + 'a' * 64)
command = args[index + 1:]
env = dict(args[i + 1].split('=', 1) for i, arg in enumerate(args) if arg == '--env')
if command[0] == 'bash':
    stage = 'prepare'
elif command[0] == './configure':
    stage = 'configure'
elif command == ['make', 'clean']:
    stage = 'clean'
elif command[0] == 'make':
    stage = 'build'
else:
    assert command == ['./x86_64-softmmu/qemu-system-x86_64', '--version']
    stage = 'version'
print(stage + ' output', flush=True)
if env.get('FAIL_STAGE') == stage:
    sys.exit(7)
qemu = root / 'qemu'
if stage == 'prepare':
    assert command[-1] == '--legacy'
    target = qemu / 'hw/vssim/simulator'
    (target / 'osc-osd/osd-target').mkdir(parents=True)
    (target / 'ftl.c').write_text('prepared ftl source\\n')
    (target / 'osc-osd/osd-target/osd-schema.c').write_text('prepared schema\\n')
elif stage == 'configure':
    assert '--enable-vssim' in command and '--disable-git-update' in command
    assert 'symlink "$vssim_source/' not in (qemu / 'configure').read_text()
    (qemu / 'config-host.mak').write_text('CONFIG_VSSIM=y\\n')
    (qemu / 'stale.o').write_text('must be cleaned')
elif stage == 'clean':
    assert (qemu / 'config-host.mak').is_file()
    (qemu / 'stale.o').unlink()
elif stage == 'build':
    assert not (qemu / 'stale.o').exists()
    (qemu / 'x86_64-softmmu').mkdir()
    (qemu / 'x86_64-softmmu/qemu-system-x86_64').write_text('legacy executable')
elif stage == 'version':
    assert (qemu / 'x86_64-softmmu/qemu-system-x86_64').is_file()
    print('QEMU emulator version 2.12.50')
''')
        docker.chmod(0o755)
        patcher = mock.patch.dict(os.environ, {"PATH": str(self.bin) + os.pathsep + os.environ["PATH"]})
        patcher.start()
        self.addCleanup(patcher.stop)

    def build(self, identity=None):
        return artifact.build(identity or self.identify(), str(self.qemu), str(self.simulator),
                              self.destination, 2)

    def test_legacy_build_preserves_make_recipe_pins_and_development_sources(self):
        original = (self.qemu / "configure").read_bytes()
        result = self.build()
        exported = Path(result["qemu_dir"])
        self.assertEqual("built", result["status"])
        self.assertEqual(self.pins, result["subprojects"])
        self.assertEqual(["prepare.log", "configure.log", "clean.log", "build.log", "version.log"],
                         [Path(c["log"]).name for c in result["commands"]])
        for command in result["commands"]:
            self.assertEqual(0, command["exit_status"])
            self.assertIn(IMAGE["image_id"], command["argv"])
            self.assertNotIn(self.request["container_image"], command["argv"])
        configure = result["commands"][1]["argv"]
        index = configure.index(IMAGE["image_id"]) + 1
        self.assertEqual(["./configure", "--target-list=x86_64-softmmu"] + self.request["configure_args"], configure[index:])
        self.assertTrue((exported / "x86_64-softmmu/qemu-system-x86_64").is_file())
        self.assertFalse((exported / "stale.o").exists())
        self.assertNotIn('symlink "$vssim_source/', (exported / "configure").read_text())
        self.assertIn('LIBS="-ljson-c $LIBS"', (exported / "configure").read_text())
        self.assertEqual(["ssd_monitor"], result["legacy_preparation"]["unused_absent_link_targets"])
        self.assertEqual(hashlib.sha256(original).hexdigest(), result["legacy_preparation"]["original_configure_sha256"])
        self.assertEqual(original, (self.qemu / "configure").read_bytes())
        self.assertEqual("", git(self.qemu, "status", "--porcelain"))
        for path in artifact.LEGACY_SUBMODULES:
            self.assertEqual(path + " pinned input\n", (exported / path / "source.c").read_text())
            self.assertFalse((exported / path / ".git").exists())
        self.assertFalse(any(p.is_symlink() for p in (exported / "hw/vssim/simulator").rglob("*")))

    def test_legacy_submodule_export_uses_pinned_objects_not_dirty_cache_files(self):
        identity = self.identify()
        repo = self.qemu / "dtc"
        write(repo, "source.c", "dirty cache input\n")
        write(repo, "cached.o", "untracked output\n")
        result = self.build(identity)
        exported = Path(result["qemu_dir"]) / "dtc"
        self.assertEqual("dtc pinned input\n", (exported / "source.c").read_text())
        self.assertFalse((exported / "cached.o").exists())
        self.assertEqual("dirty cache input\n", (repo / "source.c").read_text())

    def test_legacy_failure_at_each_stage_cleans_workspace_and_retains_logs(self):
        for index, stage in enumerate(("prepare", "configure", "clean", "build", "version")):
            with self.subTest(stage=stage):
                self.destination = self.root / ("failed-" + stage)
                self.request["build_environment"] = {"FAIL_STAGE": stage}
                with self.assertRaisesRegex(artifact.InputError, "exit 7"):
                    self.build()
                self.assertFalse((self.destination / "workspace").exists())
                self.assertFalse((self.destination / "build-result.json").exists())
                commands = json.loads((self.destination / "logs/commands.json").read_text())
                self.assertEqual(index + 1, len(commands))
                self.assertEqual(7, commands[-1]["exit_status"])
                self.assertIn(stage + " output", (self.destination / "logs" / (stage + ".log")).read_text())

    def test_legacy_makefile_missing_source_is_reported_before_configure(self):
        path = "hw/vssim/Makefile.objs"
        with (self.qemu / path).open("a") as stream:
            stream.write("common-obj-$(CONFIG_VSSIM) += simulator/ssd_file_operations.o\n")
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, path)
        with self.assertRaisesRegex(artifact.InputError, "missing simulator sources: ssd_file_operations.c"):
            self.build()
        commands = json.loads((self.destination / "logs/commands.json").read_text())
        self.assertEqual(["prepare.log"], [Path(c["log"]).name for c in commands])
        self.assertFalse((self.destination / "workspace").exists())
        self.assertIn("ssd_file_operations.o", (self.qemu / path).read_text())

    def test_legacy_missing_pin_or_repository_fails_before_container_launch(self):
        for fault in ("pin", "repository"):
            with self.subTest(fault=fault):
                self.destination = self.root / ("missing-" + fault)
                identity = self.identify()
                if fault == "pin":
                    identity["inputs"]["sources"]["qemu"]["submodules"].pop("capstone")
                else:
                    (self.qemu / "capstone").rename(self.root / "unavailable-capstone")
                with self.assertRaises(artifact.InputError):
                    self.build(identity)
                self.assertEqual([], json.loads((self.destination / "logs/commands.json").read_text()))
                self.assertFalse((self.destination / "workspace").exists())

    def test_legacy_unrecognized_configure_preparation_fails_before_configure(self):
        original = (self.qemu / "configure").read_text()
        write(self.qemu, "configure", original.replace('    make -C "$vssim_target/osc-osd/osd-target" osd-schema.c', ""))
        self.request["sources"]["qemu"]["revision"] = commit(self.qemu, "configure")
        with self.assertRaisesRegex(artifact.InputError, "unsupported legacy VSSIM configure preparation"):
            self.build()
        self.assertFalse((self.destination / "logs/configure.log").exists())

    def test_legacy_guards_reject_conflicting_flags_and_meson_cache_override(self):
        for extra in ("--disable-vssim", "--enable-git-update"):
            with self.subTest(extra=extra):
                identity = copy.deepcopy(self.identify())
                identity["inputs"]["configure_args"].append(extra)
                with self.assertRaisesRegex(artifact.InputError, "legacy isolated build requires"):
                    self.build(identity)
                self.assertFalse(self.destination.exists())
        with self.assertRaisesRegex(artifact.InputError, "--subproject-cache is for Meson"):
            artifact.build(self.identify(), str(self.qemu), str(self.simulator), self.destination, 2, self.root)
        self.assertFalse(self.destination.exists())

    def test_saved_legacy_inputs_reach_build_only_cli(self):
        record = self.identify()
        saved = self.root / "saved-inputs.json"
        saved.write_text(json.dumps(record))
        stdout, stderr = io.StringIO(), io.StringIO()
        args = [str(HELPER), "--saved-inputs", str(saved), "--qemu-dir", str(self.qemu),
                "--simulator-dir", str(self.simulator), "--build-dir", str(self.destination), "--jobs", "2"]
        with mock.patch.object(sys, "argv", args), mock.patch.object(sys, "stdout", stdout), mock.patch.object(sys, "stderr", stderr):
            status = artifact.main()
        self.assertEqual(0, status, stderr.getvalue())
        result = json.loads(stdout.getvalue())
        self.assertEqual("built", result["status"])
        self.assertEqual(record["key"], result["identity"]["key"])
        self.assertEqual("2.12.50", result["identity"]["inputs"]["sources"]["qemu"]["version"])

    def test_legacy_selection_rejects_meson_cache_override_before_compilation(self):
        with mock.patch.object(artifact, "build") as compiler:
            with self.assertRaisesRegex(artifact.InputError, "--subproject-cache is for Meson"):
                artifact.select_artifact(self.identify(), str(self.qemu), str(self.simulator), self.destination,
                                         2, self.root / "cache", self.root / "artifacts", self.root / "dist")
            compiler.assert_not_called()
        self.assertFalse(self.destination.exists())


class PublicationFixture(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-publication-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.job = self.root / "job"
        self.qemu = self.job / "workspace/qemu"
        self.qemu.mkdir(parents=True)
        (self.job / "logs").mkdir()
        write(self.qemu, "VERSION", "11.0.0\n")
        self.inputs = {"container": IMAGE, "build_environment": {},
                       "profile": {"id": 4, "qemu": {"compileContainer": "ubuntu-26.04"}},
                       "sources": {"qemu": {"revision": "a" * 40, "target_branch": "11.0", "version": "11.0.0"},
                                   "simulator": {"revision": "b" * 40, "target_branch": "master"}},
                       "target": "x86_64-softmmu", "configure_args": ["--enable-werror", "--disable-download"]}
        self.identity = {"key": artifact.input_key(self.inputs), "inputs": self.inputs}
        self.result = {"status": "built", "identity": self.identity,
                       "qemu_dir": str(self.qemu), "commands": [{"exit_status": 0}]}
        self.artifacts = self.root / "artifacts"
        self.dist = self.root / "dist"
        self.installed_files = {"bin/qemu-system-x86_64": "executable", "bin/qemu-img": "tool",
                                "share/qemu/keymaps/en-us": "keymap"}
        self.installed_files.update({"share/qemu/" + name: "firmware " + name
                                     for name in artifact.REQUIRED_FIRMWARE})
        self.monitor_files = {"vssim/simulator/www/" + name: "monitor " + name
                              for name in ("index.html", "index.css", "favicon.ico")}
        for path, content in self.monitor_files.items():
            write(self.qemu, "hw/" + path, content)
        write(self.qemu, "build/meson-info/intro-buildoptions.json", json.dumps([
            {"name": "prefix", "value": "/usr/local"}, {"name": "bindir", "value": "bin"},
            {"name": "datadir", "value": "share"},
        ]))
        write(self.qemu, "build/meson-info/intro-installed.json", json.dumps({
            "/code/qemu/" + path: "/usr/local/" + path for path in self.installed_files
        }))
        self.real_logger = artifact.run_logged
        self.runner = mock.patch.object(artifact, "run_logged", side_effect=self.run_logged)
        self.run_mock = self.runner.start()
        self.addCleanup(self.runner.stop)

    def run_logged(self, argv, log, commands, input_data=None, timeout=None):
        commands.append({"argv": list(argv), "exit_status": 0, "log": str(log)})
        if "install" in argv:
            self.assertIn("--no-rebuild", argv)
            payload = self.job / "workspace/install/usr/local"
            for path, content in self.installed_files.items():
                write(payload, path, content)
                if path.startswith("bin/"):
                    (payload / path).chmod(0o755)
            log.write_text("installed\n")
        elif "--version" in argv:
            self.assertIn(":ro", argv[argv.index("--volume") + 1])
            self.assertIn(IMAGE["image_id"], argv)
            log.write_text("QEMU emulator version " + self.inputs["sources"]["qemu"]["version"] + "\n")
        else:
            self.assertIn("-L", argv)
            self.assertEqual(b"quit\n", input_data)
            self.assertEqual(30, timeout)
            self.assertIn("--interactive", argv)
            self.assertEqual("none", argv[argv.index("--network") + 1])
            log.write_text("paused PC initialized\n")

    def publish(self):
        return artifact.publish(self.result, self.artifacts, self.dist)


class PublicationTests(PublicationFixture):
    def test_complete_bundles_survive_removal_of_build_workspace(self):
        selection = self.publish()
        self.assertEqual("published", selection["status"])
        expected = self.artifacts / "qemu" / self.identity["key"]
        staged = self.dist / "qemu" / self.identity["key"]
        self.assertEqual(str(expected), selection["bundle"])
        self.assertEqual(str(staged / "manifest.json"), selection["manifest"])
        shutil.rmtree(self.job / "workspace")
        for folder in (expected, staged):
            manifest = artifact.verify_bundle(folder, self.identity)
            self.assertEqual(set(self.installed_files) | set(self.monitor_files), set(manifest["files"]))
            self.assertEqual("vssim/simulator/www", manifest["vssim_www_dir"])
            for path, content in self.monitor_files.items():
                self.assertEqual(content, (folder / path).read_text())
            self.assertEqual("11.0.0", manifest["version"])
            self.assertTrue(os.access(folder / "bin/qemu-img", os.X_OK))
            self.assertFalse(any(p.is_symlink() for p in folder.rglob("*")))
        commands = json.loads((self.job / "logs/publication-commands.json").read_text())
        self.assertEqual(5, len(commands))
        self.assertFalse((self.job / "workspace/install").exists())

    def test_missing_linked_or_empty_monitor_assets_prevent_publication(self):
        source = self.qemu / "hw/vssim/simulator/www/index.html"
        original = source.read_bytes()
        for problem in ("missing", "link", "empty"):
            with self.subTest(problem=problem):
                self.artifacts = self.root / ("monitor-" + problem)
                source.unlink()
                if problem == "link":
                    source.symlink_to(self.qemu / "VERSION")
                elif problem == "empty":
                    source.touch()
                try:
                    with self.assertRaisesRegex(artifact.InputError, "monitor"):
                        self.publish()
                    self.assertFalse((self.artifacts / "qemu" / self.identity["key"]).exists())
                finally:
                    if source.is_symlink() or source.exists():
                        source.unlink()
                    source.write_bytes(original)

    def test_monitor_assets_are_required_even_with_matching_inventory(self):
        selection = self.publish()
        for problem in ("missing", "empty"):
            with self.subTest(problem=problem):
                folder = self.root / ("incomplete-monitor-" + problem)
                shutil.copytree(selection["bundle"], folder)
                asset = folder / "vssim/simulator/www/index.css"
                if problem == "missing":
                    asset.unlink()
                else:
                    asset.write_bytes(b"")
                manifest = json.loads((folder / "manifest.json").read_text())
                manifest["files"] = artifact.file_inventory(folder)
                (folder / "manifest.json").write_text(json.dumps(manifest))
                with self.assertRaisesRegex(artifact.InputError, "monitor"):
                    artifact.verify_bundle(folder, self.identity)

    def test_manifest_and_payload_corruption_are_rejected(self):
        selection = self.publish()
        original = Path(selection["bundle"])
        for mutation in ("content", "missing", "extra", "mode", "link", "inputs", "version", "point-version"):
            with self.subTest(mutation=mutation):
                target = self.root / mutation
                shutil.copytree(original, target)
                binary = target / "bin/qemu-system-x86_64"
                if mutation == "content":
                    binary.write_text("corrupt")
                elif mutation == "missing":
                    (target / "share/qemu/bios-256k.bin").unlink()
                elif mutation == "extra":
                    write(target, "unexpected", "unlisted")
                elif mutation == "mode":
                    binary.chmod(0o644)
                elif mutation == "link":
                    binary.unlink()
                    binary.symlink_to(original / "bin/qemu-system-x86_64")
                else:
                    path = target / "manifest.json"
                    manifest = json.loads(path.read_text())
                    if mutation == "inputs":
                        manifest["inputs"] = {}
                    else:
                        manifest["version"] = "11.0.1" if mutation == "point-version" else "2.12.0"
                    path.write_text(json.dumps(manifest))
                with self.assertRaises(artifact.InputError):
                    artifact.verify_bundle(target, self.identity)

    def test_bad_version_and_failed_firmware_probe_are_not_published(self):
        normal = self.run_logged
        for stage in ("version", "firmware"):
            with self.subTest(stage=stage):
                self.artifacts = self.root / ("artifacts-" + stage)
                def failing(argv, log, commands, input_data=None, timeout=None):
                    normal(argv, log, commands, input_data, timeout)
                    if stage == "version" and "--version" in argv:
                        log.write_text("QEMU emulator version 2.12.0\n")
                    elif stage == "firmware" and "-L" in argv:
                        raise artifact.InputError("firmware initialization failed")
                self.run_mock.side_effect = failing
                with mock.patch.object(artifact, "command_output", return_value=b""):
                    with self.assertRaises(artifact.InputError):
                        self.publish()
                parent = self.artifacts / "qemu"
                self.assertFalse((parent / self.identity["key"]).exists())
                self.assertEqual([parent / ("." + self.identity["key"] + ".lock")], list(parent.iterdir()))
                self.assertFalse((self.job / "workspace/install").exists())

    def test_failed_install_and_payload_copy_leave_no_completed_bundle(self):
        with mock.patch.object(artifact, "run_logged", side_effect=artifact.InputError("install failed")):
            with self.assertRaisesRegex(artifact.InputError, "install failed"):
                self.publish()
        self.assertFalse(self.artifacts.exists())
        with mock.patch.object(artifact.shutil, "copytree", side_effect=OSError("copy failed")):
            with self.assertRaisesRegex(OSError, "copy failed"):
                self.publish()
        parent = self.artifacts / "qemu"
        self.assertEqual([parent / ("." + self.identity["key"] + ".lock")], list(parent.iterdir()))
        self.assertFalse((self.job / "workspace/install").exists())

    def test_missing_firmware_linked_install_and_external_target_fail_closed(self):
        normal = self.run_logged
        for problem in ("firmware", "link", "external"):
            with self.subTest(problem=problem):
                self.artifacts = self.root / problem
                def incomplete(argv, log, commands, input_data=None, timeout=None):
                    normal(argv, log, commands, input_data, timeout)
                    if "install" in argv:
                        payload = self.job / "workspace/install/usr/local"
                        bios = payload / "share/qemu/bios-256k.bin"
                        if problem == "firmware":
                            bios.unlink()
                        elif problem == "link":
                            bios.unlink()
                            bios.symlink_to(self.qemu / "VERSION")
                        else:
                            write(self.qemu, "build/meson-info/intro-installed.json",
                                  json.dumps({"source": "/outside-prefix/file"}))
                self.run_mock.side_effect = incomplete
                with self.assertRaises(artifact.InputError):
                    self.publish()
                self.assertFalse((self.artifacts / "qemu" / self.identity["key"]).exists())

    def test_stage_failure_preserves_complete_persistent_bundle_without_selection(self):
        real_copy = shutil.copytree
        def copy(source, target, *args, **kwargs):
            if Path(source) == self.artifacts / "qemu" / self.identity["key"]:
                Path(target).mkdir()
                write(Path(target), "partial", "incomplete")
                raise OSError("dist copy failed")
            return real_copy(source, target, *args, **kwargs)
        with mock.patch.object(artifact.shutil, "copytree", side_effect=copy):
            with self.assertRaisesRegex(OSError, "dist copy failed"):
                self.publish()
        artifact.verify_bundle(self.artifacts / "qemu" / self.identity["key"], self.identity)
        self.assertFalse((self.dist / "qemu" / self.identity["key"]).exists())
        parent = self.dist / "qemu"
        self.assertEqual([parent / ("." + self.identity["key"] + ".lock")], list(parent.iterdir()))

    def test_concurrent_publishers_select_one_complete_winner(self):
        selection = self.publish()
        source = Path(selection["bundle"])
        parent = self.root / "concurrent"
        barrier = threading.Barrier(2)
        def populate(target):
            shutil.copytree(source, target)
            barrier.wait(timeout=10)
        def publish():
            return artifact.atomic_bundle(parent, self.identity, populate,
                                          self.job / "logs", [], "concurrent")
        with mock.patch.object(artifact, "probe_bundle"):
            with ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(publish) for _ in range(2)]
                results = [future.result(timeout=15) for future in futures]
        self.assertEqual(results[0], results[1])
        artifact.verify_bundle(results[0], self.identity)
        self.assertEqual(2, len(list(parent.iterdir())))  # Complete bundle and lock only.

    def test_rename_failure_and_invalid_existing_entry_are_never_overwritten(self):
        with mock.patch.object(Path, "rename", side_effect=OSError("rename failed")):
            with self.assertRaisesRegex(OSError, "rename failed"):
                self.publish()
        parent = self.artifacts / "qemu"
        self.assertFalse((parent / self.identity["key"]).exists())
        existing = parent / self.identity["key"]
        existing.mkdir()
        write(existing, "keep", "existing invalid entry")
        with self.assertRaises((OSError, artifact.InputError)):
            self.publish()
        self.assertEqual("existing invalid entry", (existing / "keep").read_text())
        self.assertFalse(any(p.name.startswith("." + self.identity["key"] + "-") for p in parent.iterdir()))

    def test_failed_build_and_overlapping_roots_are_rejected(self):
        self.result["commands"][0]["exit_status"] = 7
        with self.assertRaisesRegex(artifact.InputError, "successful full clean"):
            self.publish()
        self.result["commands"][0]["exit_status"] = 0
        with self.assertRaisesRegex(artifact.InputError, "separate roots"):
            artifact.publish(self.result, self.artifacts, self.artifacts)

    def test_probe_timeout_records_failure_and_removes_container(self):
        selection = self.publish()
        folder = Path(selection["bundle"])
        manifest = artifact.verify_bundle(folder, self.identity)
        commands = []
        with mock.patch.object(artifact, "run_logged", self.real_logger):
            with mock.patch.object(artifact.subprocess, "run", side_effect=subprocess.TimeoutExpired("docker", 30)):
                with mock.patch.object(artifact, "command_output", return_value=b"") as cleanup:
                    with self.assertRaisesRegex(artifact.InputError, "timed out"):
                        artifact.probe_bundle(folder, self.identity, manifest,
                                              self.job / "logs", commands, "timeout")
                    self.assertEqual(["docker", "rm", "--force"], cleanup.call_args[0][0][:3])
        self.assertEqual("timeout", commands[0]["exit_status"])


class LegacyPublicationFixture(PublicationFixture):
    def setUp(self):
        super().setUp()
        write(self.qemu, "VERSION", "2.12.50\n")
        self.inputs["sources"]["qemu"].update(target_branch="master", version="2.12.50")
        self.inputs["profile"] = {"id": 2, "qemu": {"compileContainer": "ubuntu-14.04"}}
        self.inputs["configure_args"] = ["--enable-vssim", "--disable-git-update"]
        self.identity = {"key": artifact.input_key(self.inputs), "inputs": self.inputs}
        self.result["identity"] = self.identity
        self.tools = ("qemu-img", "qemu-io", "qemu-nbd", "qemu-ga", "fsdev/virtfs-proxy-helper", "scsi/qemu-pr-helper")
        self.configuration = ("CONFIG_LINUX=y\nEXESUF=\nINSTALL_BLOBS=yes\nTOOLS=" +
                              " ".join(tool + "$(EXESUF)" for tool in self.tools) + "\n")
        write(self.qemu, "config-host.mak", self.configuration)
        self.makefile = ("KEYMAPS=en-us \\\n common\nifdef INSTALL_BLOBS\nBLOBS=" +
                         " \\\n ".join(artifact.REQUIRED_FIRMWARE) + "\nelse\nBLOBS=\nendif\n")
        write(self.qemu, "Makefile", self.makefile)
        self.installed_files = {"bin/qemu-system-x86_64": "legacy executable",
                                "libexec/qemu-bridge-helper": "bridge helper",
                                "share/qemu/trace-events-all": "trace definitions"}
        sources = [("x86_64-softmmu/qemu-system-x86_64", "bin/qemu-system-x86_64"),
                   ("qemu-bridge-helper", "libexec/qemu-bridge-helper"),
                   ("trace-events-all", "share/qemu/trace-events-all")]
        for tool in self.tools:
            target = "bin/" + Path(tool).name
            self.installed_files[target] = "tool " + tool
            sources.append((tool, target))
        for name in artifact.REQUIRED_FIRMWARE:
            target = "share/qemu/" + name
            self.installed_files[target] = "firmware " + name
            sources.append(("pc-bios/" + name, target))
        for name in ("en-us", "common"):
            target = "share/qemu/keymaps/" + name
            self.installed_files[target] = "keymap " + name
            sources.append(("pc-bios/keymaps/" + name, target))
        for source, target in sources:
            write(self.qemu, source, self.installed_files[target])
            if target.startswith(("bin/", "libexec/")):
                (self.qemu / source).chmod(0o755)


class LegacyPublicationTests(LegacyPublicationFixture):
    def test_complete_legacy_bundle_survives_build_removal_without_install_or_compile(self):
        selected = self.publish()
        shutil.rmtree(self.job / "workspace")
        for path in (selected["bundle"], selected["staged_bundle"]):
            folder = Path(path)
            manifest = artifact.verify_bundle(folder, self.identity)
            self.assertEqual("2.12.50", manifest["version"])
            self.assertEqual(set(self.installed_files) | set(self.monitor_files), set(manifest["files"]))
            for name, content in self.installed_files.items():
                self.assertEqual(content, (folder / name).read_text())
            self.assertFalse(any(file.is_symlink() for file in folder.rglob("*")))
        commands = json.loads((self.job / "logs/publication-commands.json").read_text())
        self.assertEqual(4, len(commands))
        self.assertFalse(any("make" in entry["argv"] or "install" in entry["argv"] for entry in commands))
        for entry in commands:
            if "-L" in entry["argv"]:
                self.assertIn("-net", entry["argv"])
                self.assertNotIn("-nic", entry["argv"])

    def test_legacy_unsupported_or_ambiguous_configuration_is_rejected(self):
        configurations = [self.configuration + "CONFIG_MODULES=y\n",
                          self.configuration.replace("INSTALL_BLOBS=yes", "INSTALL_BLOBS=no"),
                          self.configuration.replace("EXESUF=\n", "EXESUF=.exe\n"),
                          self.configuration + "TOOLS=qemu-img\n",
                          "\n".join(line for line in self.configuration.splitlines() if not line.startswith("TOOLS="))]
        for index, text in enumerate(configurations):
            with self.subTest(index=index):
                self.artifacts = self.root / ("configuration-" + str(index))
                write(self.qemu, "config-host.mak", text)
                with self.assertRaises(artifact.InputError):
                    self.publish()
                self.assertFalse(self.artifacts.exists())
                self.assertFalse((self.job / "workspace/install").exists())

    def test_legacy_install_lists_cannot_evaluate_or_escape_paths(self):
        for index, invalid in enumerate(("../outside", "/outside", "$(shell touch MUST_NOT_EXIST)", "./bios.bin")):
            with self.subTest(invalid=invalid):
                self.artifacts = self.root / ("paths-" + str(index))
                write(self.qemu, "Makefile", self.makefile.replace("BLOBS=bios-256k.bin", "BLOBS=" + invalid))
                with self.assertRaisesRegex(artifact.InputError, "unsupported legacy install input"):
                    self.publish()
                self.assertFalse((self.qemu / "MUST_NOT_EXIST").exists())
                self.assertFalse(self.artifacts.exists())

    def test_missing_or_nonexecutable_enabled_tool_prevents_legacy_publication(self):
        tool = self.qemu / "scsi/qemu-pr-helper"
        tool.unlink()
        with self.assertRaisesRegex(artifact.InputError, "nonempty regular file"):
            self.publish()
        write(self.qemu, "scsi/qemu-pr-helper", "tool")
        tool.chmod(0o644)
        with self.assertRaisesRegex(artifact.InputError, "not executable"):
            self.publish()
        self.assertFalse(self.artifacts.exists())
        self.assertFalse((self.job / "workspace/install").exists())

    def test_linked_firmware_or_source_parent_prevents_legacy_publication(self):
        bios = self.qemu / "pc-bios/bios-256k.bin"
        bios.unlink()
        bios.symlink_to(self.qemu / "VERSION")
        with self.assertRaisesRegex(artifact.InputError, "nonempty regular file"):
            self.publish()
        bios.unlink()
        write(self.qemu, "pc-bios/bios-256k.bin", "firmware")
        source = self.qemu / "pc-bios"
        outside = self.root / "linked-firmware"
        source.rename(outside)
        source.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(artifact.InputError, "nonempty regular file"):
            self.publish()
        self.assertFalse(self.artifacts.exists())

    def test_legacy_copy_failure_leaves_no_install_or_bundle(self):
        with mock.patch.object(artifact.shutil, "copy2", side_effect=OSError("copy failed")):
            with self.assertRaisesRegex(OSError, "copy failed"):
                self.publish()
        self.assertFalse(self.artifacts.exists())
        self.assertFalse((self.job / "workspace/install").exists())

    def test_legacy_wrong_version_and_failed_firmware_are_not_published(self):
        normal = self.run_logged
        for stage in ("version", "firmware"):
            with self.subTest(stage=stage):
                self.artifacts = self.root / stage
                def failing(argv, log, commands, input_data=None, timeout=None):
                    normal(argv, log, commands, input_data, timeout)
                    if stage == "version" and "--version" in argv:
                        log.write_text("QEMU emulator version 11.0.0\n")
                    elif stage == "firmware" and "-L" in argv:
                        raise artifact.InputError("firmware failed")
                self.run_mock.side_effect = failing
                with mock.patch.object(artifact, "command_output", return_value=b""):
                    with self.assertRaises(artifact.InputError):
                        self.publish()
                parent = self.artifacts / "qemu"
                self.assertFalse((parent / self.identity["key"]).exists())
                self.assertFalse((self.job / "workspace/install").exists())
                self.assertEqual([parent / ("." + self.identity["key"] + ".lock")], list(parent.iterdir()))


class CacheTests(PublicationFixture):
    def select(self, identity=None, name="attempt"):
        return artifact.select_artifact(identity or self.identity, str(self.qemu), "unused-simulator",
                                        self.root / name, 4, None,
                                        self.artifacts, self.dist)

    def test_hit_skips_build_and_returns_identified_paths(self):
        self.publish()
        legacy = self.root / "legacy/qemu-system-x86_64"
        write(self.root, "legacy/qemu-system-x86_64", "legacy binary")
        with mock.patch.object(artifact, "build") as build:
            selected = self.select()
        build.assert_not_called()
        self.assertEqual("selected", selected["status"])
        self.assertEqual("hit", selected["cache"])
        self.assertEqual(self.identity["inputs"]["profile"]["id"], selected["profile_id"])
        self.assertEqual(IMAGE["image_id"], selected["container_image_id"])
        self.assertEqual(self.identity["inputs"]["sources"]["qemu"]["version"], selected["version"])
        self.assertTrue(Path(selected["executable"]).is_file())
        self.assertTrue(Path(selected["data_dir"]).is_dir())
        self.assertEqual(self.identity["key"], json.loads(Path(selected["manifest"]).read_text())["key"])
        self.assertEqual(hashlib.sha256(Path(selected["executable"]).read_bytes()).hexdigest(), selected["binary_sha256"])
        self.assertEqual("legacy binary", legacy.read_text())
        self.assertFalse((self.root / "attempt/workspace").exists())
        self.assertFalse((self.root / "attempt/logs/build.log").exists())
        self.assertEqual("selected", json.loads((self.root / "attempt/logs/cache.json").read_text())["status"])

    def test_valid_staged_hit_needs_no_copy_or_temporary_space(self):
        published = self.publish()
        with mock.patch.object(artifact.shutil, "copytree",
                               side_effect=OSError(errno.ENOSPC, "No space left on device")) as copy_payload:
            with mock.patch.object(artifact.tempfile, "TemporaryDirectory",
                                   side_effect=OSError(errno.ENOSPC, "No space left on device")) as temporary:
                selected = self.select()
        copy_payload.assert_not_called()
        temporary.assert_not_called()
        self.assertEqual("hit", selected["cache"])
        self.assertEqual(published["staged_bundle"], selected["staged_bundle"])
        self.assertEqual(str(Path(selected["staged_bundle"]) / "vssim/simulator/www"),
                         selected["vssim_www_dir"])
        artifact.verify_bundle(selected["staged_bundle"], self.identity)

    def test_miss_builds_once_then_identical_request_hits(self):
        old_job = self.job
        def build(identity, qemu_dir, simulator_dir, destination, jobs, cache):
            self.assertEqual(4, jobs)
            self.job = Path(destination)
            shutil.copytree(old_job, self.job)
            self.qemu = self.job / "workspace/qemu"
            return dict(self.result, identity=identity, qemu_dir=str(self.qemu))
        with mock.patch.object(artifact, "build", side_effect=build) as compiler:
            first = self.select(name="miss")
            second = self.select(name="hit")
        self.assertEqual(1, compiler.call_count)
        self.assertEqual(("miss", "hit"), (first["cache"], second["cache"]))
        self.assertEqual(first["manifest"], second["manifest"])

    def test_missing_dist_is_restaged_from_verified_persistent_bundle(self):
        self.publish()
        shutil.rmtree(self.dist)
        with mock.patch.object(artifact, "build") as compiler:
            selected = self.select()
        compiler.assert_not_called()
        artifact.verify_bundle(Path(selected["staged_bundle"]), self.identity)

    def test_corrupt_or_incomplete_persistent_entries_fail_without_compiling(self):
        selected = self.publish()
        persistent = Path(selected["bundle"])
        for problem in ("checksum", "manifest"):
            with self.subTest(problem=problem):
                path = persistent / ("bin/qemu-system-x86_64" if problem == "checksum" else "manifest.json")
                original = path.read_bytes()
                if problem == "checksum":
                    path.write_text("corrupt binary")
                else:
                    path.unlink()
                with mock.patch.object(artifact, "build") as compiler:
                    with self.assertRaises((artifact.InputError, OSError)):
                        self.select(name=problem)
                compiler.assert_not_called()
                decision = json.loads((self.root / problem / "logs/cache.json").read_text())
                self.assertEqual("rejected", decision["status"])
                self.assertIn("error", decision)
                self.assertFalse((self.root / problem / "workspace").exists())
                path.write_bytes(original)

    def test_changed_profile_selects_new_key_and_requires_a_build(self):
        old = self.publish()
        changed = copy.deepcopy(self.identity)
        changed["inputs"]["profile"]["id"] = 1
        changed["key"] = artifact.input_key(changed["inputs"])
        with mock.patch.object(artifact, "build", side_effect=artifact.InputError("clean build requested")) as compiler:
            with self.assertRaisesRegex(artifact.InputError, "clean build requested"):
                self.select(changed)
        self.assertEqual(changed["key"], compiler.call_args[0][0]["key"])
        artifact.verify_bundle(Path(old["bundle"]), self.identity)
        self.assertNotEqual(old["key"], changed["key"])

    def test_runtime_and_staging_failures_never_return_a_hit(self):
        old = self.publish()
        with mock.patch.object(artifact, "build") as compiler:
            with mock.patch.object(artifact, "probe_bundle", side_effect=artifact.InputError("runtime failed")):
                with self.assertRaisesRegex(artifact.InputError, "runtime failed"):
                    self.select(name="runtime-failure")
            write(Path(old["staged_bundle"]), "unexpected", "invalid stage")
            with self.assertRaisesRegex(artifact.InputError, "inventory"):
                self.select(name="stage-failure")
        compiler.assert_not_called()
        artifact.verify_bundle(Path(old["bundle"]), self.identity)

    def test_existing_job_and_invalid_roots_fail_before_building(self):
        (self.root / "attempt").mkdir()
        write(self.root, "attempt/keep", "existing job")
        with mock.patch.object(artifact, "build") as compiler:
            with self.assertRaisesRegex(artifact.InputError, "new job"):
                self.select()
            with self.assertRaisesRegex(artifact.InputError, "separate roots"):
                artifact.select_artifact(self.identity, str(self.qemu), "unused", self.root / "new",
                                         4, None, self.artifacts, self.artifacts)
        compiler.assert_not_called()
        self.assertEqual("existing job", (self.root / "attempt/keep").read_text())


class LegacyCacheTests(LegacyPublicationFixture, CacheTests):
    def test_changed_legacy_inputs_require_new_key_and_preserve_old_bundle(self):
        old = self.publish()
        for index, field in enumerate(("source", "container", "flags", "recipe")):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.identity)
                if field == "source":
                    changed["inputs"]["sources"]["simulator"]["revision"] = "c" * 40
                elif field == "container":
                    changed["inputs"]["container"]["image_id"] = "sha256:" + "c" * 64
                elif field == "flags":
                    changed["inputs"]["configure_args"].append("--disable-docs")
                else:
                    changed["inputs"]["recipes"] = {"preparation": "changed"}
                changed["key"] = artifact.input_key(changed["inputs"])
                with mock.patch.object(artifact, "build", side_effect=artifact.InputError("clean build required")) as compiler:
                    with self.assertRaisesRegex(artifact.InputError, "clean build required"):
                        self.select(changed, name="changed-" + str(index))
                compiler.assert_called_once()
                self.assertNotEqual(old["key"], changed["key"])
                artifact.verify_bundle(old["bundle"], self.identity)

    def test_both_versions_coexist_and_reuse_without_changing_the_other_bundle(self):
        legacy = self.publish()
        legacy_identity = copy.deepcopy(self.identity)
        modern = PublicationFixture(methodName="runTest")
        modern.setUp()
        try:
            modern.artifacts, modern.dist = self.artifacts, self.dist
            current = modern.publish()
            modern_before = artifact.file_inventory(current["bundle"])
            legacy_before = artifact.file_inventory(legacy["bundle"])
        finally:
            modern.doCleanups()
        self.assertNotEqual(legacy["key"], current["key"])
        with mock.patch.object(artifact, "build") as compiler:
            selected_legacy = self.select(name="legacy-reuse")
            with mock.patch.object(artifact, "run_logged", side_effect=modern.run_logged):
                selected_modern = artifact.select_artifact(modern.identity, "unused", "unused",
                    self.root / "modern-reuse", 4, None, self.artifacts, self.dist)
        compiler.assert_not_called()
        self.assertEqual(("hit", "hit"), (selected_legacy["cache"], selected_modern["cache"]))
        for path in (legacy["bundle"], legacy["staged_bundle"]):
            artifact.verify_bundle(path, legacy_identity)
            self.assertEqual(legacy_before, artifact.file_inventory(path))
        for path in (current["bundle"], current["staged_bundle"]):
            artifact.verify_bundle(path, modern.identity)
            self.assertEqual(modern_before, artifact.file_inventory(path))


class CacheCliTests(InputFixture):
    legacy = False
    input_mode = "--request"

    def test_compile_entrypoint_selects_hit_and_rejects_corruption(self):
        if self.legacy:
            write(self.qemu, "VERSION", "2.12.50\n")
            self.request["sources"]["qemu"].update(revision=commit(self.qemu, "VERSION"), target_branch="master")
            self.request["profile"] = json.loads((HELPER.parent / "versions/configs.json").read_text())["configs"][1]
            self.request["dependencies"] = []
            self.request["configure_args"] = ["--enable-vssim", "--disable-git-update"]
        else:
            self.request["configure_args"].append("--disable-download")
        identity = self.identify()
        version = identity["inputs"]["sources"]["qemu"]["version"]
        persistent = self.root / "artifacts/qemu" / identity["key"]
        write(persistent, "bin/qemu-system-x86_64", "executable")
        (persistent / "bin/qemu-system-x86_64").chmod(0o755)
        for name in artifact.REQUIRED_FIRMWARE:
            write(persistent, "share/qemu/" + name, "firmware")
        for name in ("index.html", "index.css", "favicon.ico"):
            write(persistent, "vssim/simulator/www/" + name, "monitor asset")
        manifest = {"schema_version": 1, "key": identity["key"], "inputs": identity["inputs"],
                    "version": version, "executable": "bin/qemu-system-x86_64", "data_dir": "share/qemu",
                    "vssim_www_dir": "vssim/simulator/www",
                    "files": artifact.file_inventory(persistent), "build": "full-clean-passed"}
        write(persistent, "manifest.json", json.dumps(manifest))
        docker = self.root / "bin/docker"
        docker.parent.mkdir()
        docker.write_text("#!" + sys.executable + "\n" + '''
import json, os, sys
args = sys.argv[1:]
if args[:2] == ['image', 'inspect']:
    print(json.dumps({'Id': 'sha256:' + 'a' * 64, 'Os': 'linux', 'Architecture': 'amd64'}))
elif '--version' in args:
    assert args[0] == 'run' and '--name' in args
    print('QEMU emulator version ' + os.environ['S2_CACHE_TEST_VERSION'])
else:
    assert '-L' in args and '-S' in args, 'compilation must never run on a hit'
    assert sys.stdin.buffer.read() == b'quit\\n'
''')
        docker.chmod(0o755)
        request_path = self.root / "request.json"
        request_path.write_text(json.dumps(identity if self.input_mode == "--saved-inputs" else self.request))
        with mock.patch.dict(os.environ, {"PATH": str(docker.parent) + os.pathsep + os.environ["PATH"],
                                          "S2_CACHE_TEST_VERSION": version}):
            for corrupt in (False, True):
                if corrupt:
                    write(persistent, "bin/qemu-system-x86_64", "corrupt")
                job = self.root / ("invalid" if corrupt else "valid")
                entrypoint = ([sys.executable, str(HELPER)] if self.input_mode == "--saved-inputs"
                              else ["bash", str(HELPER.parent / "compile-qemu.sh")])
                process = subprocess.run(entrypoint + [
                    self.input_mode, str(request_path),
                    "--qemu-dir", str(self.qemu), "--simulator-dir", str(self.simulator),
                    "--build-dir", str(job), "--artifact-root", str(self.root / "artifacts"),
                    "--dist-root", str(self.root / "dist"),
                ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if corrupt:
                    self.assertEqual(1, process.returncode)
                    self.assertEqual(b"", process.stdout)
                    self.assertIn(b"checksums", process.stderr)
                else:
                    self.assertEqual(0, process.returncode, process.stderr.decode())
                    selected = json.loads(process.stdout)
                    self.assertEqual("hit", selected["cache"])
                    self.assertEqual("selected", selected["status"])
                    self.assertEqual(version, selected["version"])
                    self.assertEqual(self.request["sources"]["qemu"]["revision"], selected["sources"]["qemu"])
                self.assertFalse((job / "workspace").exists())


class LegacyCacheCliTests(CacheCliTests):
    legacy = True


class LegacySavedCacheCliTests(LegacyCacheCliTests):
    input_mode = "--saved-inputs"


class PreparationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Use committed authoritative inputs, excluding local build outputs.
        cls.source_archive = subprocess.check_output([
            "git", "-C", str(HELPER.parents[2]), "archive", "HEAD",
            "eVSSIM/CONFIG", "eVSSIM/FTL_SOURCE", "eVSSIM/LOG_MGR",
            "eVSSIM/MONITOR/SERVER", "eVSSIM/SSD_MODULE",
            "eVSSIM/osc-osd/osd-target", "eVSSIM/osc-osd/osd-util",
        ])

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-preparation-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        with tarfile.open(fileobj=io.BytesIO(self.source_archive)) as archive:
            archive.extractall(str(self.root))
        self.source = self.root / "eVSSIM"
        self.qemu = self.root / "qemu"
        self.vssim = self.qemu / "hw/vssim"
        self.vssim.mkdir(parents=True)
        self.target = self.vssim / "simulator"

    def prepare(self, env=None, legacy=False):
        process = subprocess.Popen(
            ["bash", str(HELPER.with_name("prepare-qemu-vssim.sh")), str(self.qemu), str(self.source)] +
            (["--legacy"] if legacy else []),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
        )
        stdout, stderr = process.communicate()
        return process.returncode, stdout, stderr

    def assert_prepared(self):
        status, stdout, stderr = self.prepare()
        self.assertEqual(0, status, (stdout, stderr))
        self.assertIn(b"INFO prepared", stdout)
        self.assertEqual([], list(self.vssim.glob(".simulator.*")))

    def test_preparation_does_not_require_the_missing_legacy_realpath_command(self):
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        stub = bin_dir / "realpath"
        stub.write_text("#!/bin/sh\nexit 127\n")
        stub.chmod(0o755)
        env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
        for legacy in (False, True):
            with self.subTest(legacy=legacy):
                status, stdout, stderr = self.prepare(env=env, legacy=legacy)
                self.assertEqual(0, status, (stdout, stderr))
                self.assertFalse(any(path.is_symlink() for path in self.target.rglob("*")))

    def test_regular_files_preserve_osd_layout_headers_assets_and_schema(self):
        schema = self.source / "osc-osd/osd-target/osd.schema"
        schema.write_text("CREATE TABLE test (id INTEGER);\n")
        self.assert_prepared()
        for path in self.target.rglob("*"):
            self.assertFalse(path.is_symlink(), str(path))
        for name in ("attr", "cdb", "coll", "db", "list-entry", "mtq", "obj", "osd"):
            for suffix in (".c", ".h"):
                relative = "osc-osd/osd-target/" + name + suffix
                self.assertEqual((self.source / relative).read_bytes(), (self.target / relative).read_bytes())
        self.assertTrue((self.target / "osc-osd/osd-util/osd-defs.h").is_file())
        self.assertEqual({"index.html", "index.css", "favicon.ico"},
                         {p.name for p in (self.target / "www").iterdir()})
        self.assertEqual(b'const char osd_schema[] =\n"CREATE TABLE test (id INTEGER);\\n"\n;\n',
                         (self.target / "osd-schema.c").read_bytes())
        # Quoted local includes must resolve in the existing QEMU include layout.
        for path in self.target.rglob("*"):
            if path.suffix not in (".c", ".h"):
                continue
            for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.MULTILINE):
                candidates = (path.parent / include, self.target / include, self.target / "osc-osd" / include)
                self.assertTrue(any(candidate.is_file() for candidate in candidates), (path, include))

    def test_legacy_preparation_copies_optional_sources_and_nested_schema_as_regular_files(self):
        optional = {
            "FTL_SOURCE/COMMON/ssd_file_operations/ssd_file_operations.c": "file operations source\n",
            "FTL_SOURCE/COMMON/ssd_file_operations/ssd_file_operations.h": "file operations header\n",
            "FTL_SOURCE/PERF_MODULE/ftl_perf_manager.c": "performance source\n",
            "FTL_SOURCE/PERF_MODULE/ftl_perf_manager.h": "performance header\n",
        }
        for path, content in optional.items():
            write(self.source, path, content)
        status, stdout, stderr = self.prepare(legacy=True)
        self.assertEqual(0, status, (stdout, stderr))
        self.assertEqual((self.target / "osd-schema.c").read_bytes(),
                         (self.target / "osc-osd/osd-target/osd-schema.c").read_bytes())
        for path, content in optional.items():
            self.assertEqual(content, (self.target / Path(path).name).read_text())
        self.assertFalse(any(path.is_symlink() for path in self.target.rglob("*")))
        # Modern preparation still excludes these legacy-only files.
        self.assert_prepared()
        for path in optional:
            self.assertFalse((self.target / Path(path).name).exists())
        self.assertFalse((self.target / "osc-osd/osd-target/osd-schema.c").exists())

    def test_legacy_preparation_does_not_restore_deleted_optional_sources(self):
        status, stdout, stderr = self.prepare(legacy=True)
        self.assertEqual(0, status, (stdout, stderr))
        self.assertTrue((self.target / "osc-osd/osd-target/osd-schema.c").is_file())
        self.assertFalse((self.target / "ssd_file_operations.c").exists())
        self.assertFalse((self.target / "ftl_perf_manager.c").exists())

    def test_legacy_optional_source_links_are_rejected_without_replacing_previous_view(self):
        self.assert_prepared()
        previous = (self.target / "onfi.h").read_bytes()
        optional = self.source / "FTL_SOURCE/PERF_MODULE/ftl_perf_manager.c"
        optional.parent.mkdir(parents=True)
        optional.symlink_to(self.source / "SSD_MODULE/onfi.c")
        status, stdout, stderr = self.prepare(legacy=True)
        self.assertNotEqual(0, status, (stdout, stderr))
        self.assertIn(b"non-regular", stdout)
        self.assertEqual(previous, (self.target / "onfi.h").read_bytes())
        self.assertEqual([], list(self.vssim.glob(".simulator.*")))

    def test_preparation_refreshes_copies_and_removes_stale_files(self):
        self.assert_prepared()
        original = (self.target / "onfi.h").read_bytes()
        write(self.source, "SSD_MODULE/onfi.h", "changed header\n")
        self.assertEqual(original, (self.target / "onfi.h").read_bytes())
        write(self.target, "obsolete.c", "stale generated file\n")
        self.assert_prepared()
        self.assertEqual(b"changed header\n", (self.target / "onfi.h").read_bytes())
        self.assertFalse((self.target / "obsolete.c").exists())

    def test_unlisted_outputs_and_links_are_not_copied(self):
        write(self.source, "osc-osd/osd-target/osd.o", "object output\n")
        write(self.source, "osc-osd/tgt/usr/unused.c", "unrelated source\n")
        write(self.source, "MONITOR/SERVER/www/unrelated.txt", "untracked output\n")
        os.symlink(".", str(self.source / "MONITOR/SERVER/www/www"))
        self.assert_prepared()
        self.assertFalse((self.target / "osc-osd/osd-target/osd.o").exists())
        self.assertFalse((self.target / "osc-osd/tgt").exists())
        self.assertFalse((self.target / "www/unrelated.txt").exists())
        self.assertFalse((self.target / "www/www").exists())

    def test_missing_required_input_fails_without_replacing_previous_view(self):
        self.assert_prepared()
        before = (self.target / "onfi.h").read_bytes()
        (self.source / "SSD_MODULE/onfi.h").unlink()
        status, stdout, stderr = self.prepare()
        self.assertNotEqual(0, status, stderr)
        self.assertNotIn(b"INFO prepared", stdout)
        self.assertIn(b"missing or non-regular", stdout)
        self.assertEqual(before, (self.target / "onfi.h").read_bytes())
        self.assertEqual([], list(self.vssim.glob(".simulator.*")))

    def test_required_source_file_link_is_rejected(self):
        header = self.source / "SSD_MODULE/onfi.h"
        header.rename(self.root / "outside-header")
        header.symlink_to(self.root / "outside-header")
        status, stdout, stderr = self.prepare()
        self.assertNotEqual(0, status, stderr)
        self.assertIn(b"non-regular", stdout)
        self.assertFalse(self.target.exists())
        self.assertEqual([], list(self.vssim.glob(".simulator.*")))

    def test_required_source_parent_link_is_rejected(self):
        directory = self.source / "osc-osd"
        directory.rename(self.root / "outside-osd")
        directory.symlink_to(self.root / "outside-osd", target_is_directory=True)
        status, stdout, stderr = self.prepare()
        self.assertNotEqual(0, status, stderr)
        self.assertIn(b"linked eVSSIM source directory", stdout)
        self.assertFalse(self.target.exists())

    def test_missing_required_web_asset_fails(self):
        (self.source / "MONITOR/SERVER/www/index.css").unlink()
        status, stdout, stderr = self.prepare()
        self.assertNotEqual(0, status, stderr)
        self.assertIn(b"index.css", stdout)
        self.assertFalse(self.target.exists())

    def test_old_generated_links_are_replaced_without_following_them(self):
        self.target.mkdir()
        (self.target / "osc-osd").symlink_to(self.source / "osc-osd", target_is_directory=True)
        (self.target / "www").symlink_to(self.source / "MONITOR/SERVER/www", target_is_directory=True)
        original = (self.source / "osc-osd/osd-target/osd.c").read_bytes()
        self.assert_prepared()
        self.assertFalse((self.target / "osc-osd").is_symlink())
        self.assertFalse((self.target / "www").is_symlink())
        self.assertEqual(original, (self.source / "osc-osd/osd-target/osd.c").read_bytes())

    def test_linked_destination_parent_is_rejected(self):
        self.vssim.rename(self.root / "outside-vssim")
        self.vssim.symlink_to(self.root / "outside-vssim", target_is_directory=True)
        status, stdout, stderr = self.prepare()
        self.assertNotEqual(0, status, stderr)
        self.assertIn(b"parent directories", stdout)
        self.assertEqual([], list((self.root / "outside-vssim").iterdir()))

    def test_copy_and_schema_failures_propagate_and_clean_temporary_files(self):
        self.assert_prepared()
        original = (self.target / "osd-schema.c").read_bytes()
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        env = dict(os.environ, PATH=str(bin_dir) + os.pathsep + os.environ["PATH"])
        for tool in ("cp", "sed"):
            with self.subTest(tool=tool):
                stub = bin_dir / tool
                stub.write_text("#!/bin/sh\nexit 7\n")
                stub.chmod(0o755)
                status, stdout, stderr = self.prepare(env=env)
                self.assertEqual(7, status, (stdout, stderr))
                self.assertNotIn(b"INFO prepared", stdout)
                self.assertEqual(original, (self.target / "osd-schema.c").read_bytes())
                self.assertEqual([], list(self.vssim.glob(".simulator.*")))
                stub.unlink()


class DockerTests(unittest.TestCase):
    def test_actual_inspected_id_is_used(self):
        with mock.patch.object(artifact, "command_output", return_value=json.dumps({
            "Id": IMAGE["image_id"], "Os": "linux", "Architecture": "amd64"
        }).encode()) as command:
            self.assertEqual(IMAGE, artifact.inspect_image("mutable:tag"))
            self.assertEqual(["docker", "image", "inspect", "--format", "{{json .}}", "--", "mutable:tag"],
                             command.call_args[0][0])

    def test_missing_or_malformed_image_identity_fails(self):
        for value in ({}, {"Id": "mutable:tag"},
                      {"Id": IMAGE["image_id"] + "\n", "Os": "linux", "Architecture": "amd64"},
                      {"Id": IMAGE["image_id"], "Os": "linux"}):
            with self.subTest(value=value):
                with mock.patch.object(artifact, "command_output", return_value=json.dumps(value).encode()):
                    with self.assertRaises(artifact.InputError):
                        artifact.inspect_image("fixture:tag")


class CanonicalTests(unittest.TestCase):
    def test_canonical_bytes_preserve_argument_order(self):
        self.assertEqual(b'{"a":[2,1],"z":1}', artifact.canonical_bytes({"z": 1, "a": [2, 1]}))
        self.assertEqual("d9428d145fb55faf5808417309afba0e0e2c33cf73adc25fe8fc19ff7532de0a",
                         artifact.input_key({"z": 1, "a": [2, 1]}))
        self.assertNotEqual(artifact.input_key({"args": ["a", "b"]}),
                            artifact.input_key({"args": ["b", "a"]}))


if __name__ == "__main__":
    unittest.main()
