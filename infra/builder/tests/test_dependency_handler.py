"""Exercise the production dependency handler with real disposable Git repos.

Only Gerrit transport and selected Git failures are mocked. No QEMU build,
profile selection, artifact helper, Docker, or external service is needed.
"""

import copy
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


HANDLER = Path(__file__).resolve().parents[1] / "handle-depend-on-instructions.sh"


def git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo)] + list(args),
                                   stderr=subprocess.PIPE).decode().strip()


def write(repo, name, content):
    path = repo / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)


def new_repo(path):
    path.mkdir()
    git(path, "init", "-q", "--initial-branch=master")
    git(path, "config", "user.name", "Dependency test")
    git(path, "config", "user.email", "dependency-test@example.invalid")
    git(path, "config", "commit.gpgsign", "false")


def commit(repo, *paths):
    git(repo, "add", "--", *paths)
    git(repo, "commit", "-q", "-m", "fixture")
    return git(repo, "rev-parse", "HEAD")


class DependencyHandlerTests(unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.temp = tempfile.TemporaryDirectory(prefix="dependency-handler-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu"
        new_repo(self.qemu)
        write(self.qemu, "source.c", "qemu input\n")
        commit(self.qemu, "source.c")
        self.url = "https://review.gerrithub.io/c/test/qemu/+/1"
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
        url = self.url
        self.responses = {"message": "Dependency fixture\n\nDepends-on: " + url,
                          "changes": {"1": {"status": "NEW", "branch": "11.0",
                              "current_revision": self.review_revision,
                              "revisions": {self.review_revision: {"ref": self.review_ref}}}}}
        self.response_file = self.root / "responses.json"
        self.report_file = self.root / "dependencies.json"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        scripts = {
            "curl": '''import json, os, re, sys
data = json.load(open(os.environ['DEPENDENCY_TEST_DATA']))
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
failure = json.loads(os.environ.get('DEPENDENCY_TEST_GIT_FAILURE', 'null'))
if failure and args[:len(failure)] == failure:
    sys.exit(1)
if len(args) > 1 and args[0] == 'fetch' and args[1].startswith('https://'):
    args[1] = os.environ['DEPENDENCY_TEST_ORIGIN']
sys.exit(subprocess.call([os.environ['DEPENDENCY_TEST_GIT']] + args))
''',
            "ssh": '''import json, os, sys
data = json.load(open(os.environ['DEPENDENCY_TEST_DATA']))
if os.environ.get('DEPENDENCY_TEST_SSH_LOG'):
    with open(os.environ['DEPENDENCY_TEST_SSH_LOG'], 'a') as log:
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
                        DEPENDENCY_TEST_GIT=shutil.which("git"),
                        DEPENDENCY_TEST_DATA=str(self.response_file),
                        DEPENDENCY_TEST_ORIGIN=str(self.origin),
                        EVSSIM_DEPENDENCY_RECORD=str(self.report_file))

    def handler(self, source="gerrit", with_record=True):
        self.response_file.write_text(json.dumps(self.responses))
        env = dict(self.env)
        if not with_record:
            env.pop("EVSSIM_DEPENDENCY_RECORD")
        return subprocess.run(["bash", str(HANDLER),
                               str(self.root), "99", "test", source], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)

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

    def test_real_cherry_pick_records_declared_and_applied_revisions(self):
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

    def test_existing_ancestor_records_its_original_revision(self):
        git(self.origin, "update-ref", "refs/heads/11.0", self.review_revision)
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        self.report = json.loads(self.report_file.read_text())
        dependency = self.report["dependencies"][0]
        self.assertEqual("already_present", dependency["disposition"])
        self.assertEqual(self.review_revision, dependency["applied_revision"])

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
        self.responses["message"] = "Dependency fixture\n\nDepends-on: missing-url"
        self.assertNotEqual(0, self.handler().returncode)
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])

    def test_failed_dependency_followed_by_success_keeps_failed_record(self):
        abandoned = self.prepend_dependency()
        abandoned["status"] = "ABANDONED"
        self.assert_failed_then_applied(self.handler())
        self.report = json.loads(self.report_file.read_text())
        self.assertEqual("failed", self.report["status"])
        self.assertEqual(["failed", "applied"], [d["disposition"] for d in self.report["dependencies"]])

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
        self.env["DEPENDENCY_TEST_GIT_FAILURE"] = json.dumps(["cherry-pick", "--skip"])
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
                self.env["DEPENDENCY_TEST_GIT_FAILURE"] = json.dumps(command)
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
        message = "Dependency fixture\n\nMultiline body.\n\nDepends-on: " + self.url
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

    def test_both_ssh_fallbacks_use_agent_account_without_changing_repository_owner(self):
        url = self.url
        self.use_ssh_commit_message(self.responses["message"])
        self.responses["rest_failures"] = ["1"]
        self.responses["ssh_changes"]["1"] = {
            "status": "NEW", "branch": "11.0", "currentPatchSet": {
                "revision": self.review_revision, "ref": self.review_ref}}
        calls = self.root / "ssh-calls.jsonl"
        self.env["DEPENDENCY_TEST_SSH_LOG"] = str(calls)
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
        self.use_ssh_commit_message("Dependency fixture\n\nBody without dependency trailers.")
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
        self.responses["message"] = "Dependency fixture without dependency trailers"
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

    def test_no_dependencies_succeeds_without_record(self):
        self.responses["message"] = "Body without dependency trailers"
        process = self.handler(with_record=False)
        self.assertEqual(0, process.returncode, process.stderr.decode())
        self.assertFalse(self.report_file.exists())

    def test_report_retains_every_supported_project(self):
        projects = ("qemu", "kernel", "nvme-cli", "dnvme", "tnvme", "open-osd", "simulator")
        trailers = []
        for number, project in enumerate(projects, 1):
            repo = self.root / project
            if project in ("dnvme", "tnvme"):
                repo = self.root / "nvmeCompl" / project
                repo.parent.mkdir(exist_ok=True)
            if project != "qemu":
                subprocess.check_output(["git", "clone", str(self.origin), str(repo)],
                                        stderr=subprocess.PIPE)
            ref = f"refs/changes/{number:02d}/{number}/7"
            git(self.origin, "update-ref", ref, self.review_revision)
            detail = copy.deepcopy(self.responses["changes"]["1"])
            detail["revisions"] = {self.review_revision: {"ref": ref}}
            self.responses["changes"][str(number)] = detail
            trailers.append(f"Depends-on: https://review.gerrithub.io/c/test/{project}/+/{number}")
        self.responses["message"] = "Dependencies across projects\n\n" + "\n".join(trailers)
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        report = json.loads(self.report_file.read_text())
        self.assertEqual("complete", report["status"])
        self.assertEqual(list(projects), [d["project"] for d in report["dependencies"]])
        self.assertTrue(all(d["disposition"] == "applied" for d in report["dependencies"]))

    def test_merged_dependency_already_in_target_branch_succeeds(self):
        self.responses["changes"]["1"]["status"] = "MERGED"
        git(self.origin, "update-ref", "refs/heads/11.0", self.review_revision)
        process = self.handler()
        self.assertEqual(0, process.returncode, process.stdout.decode() + process.stderr.decode())
        dependency = json.loads(self.report_file.read_text())["dependencies"][0]
        self.assertEqual("already_present", dependency["disposition"])
        self.assertEqual(self.review_revision, dependency["applied_revision"])

    def test_missing_record_parent_fails_before_dependency_application(self):
        self.env["EVSSIM_DEPENDENCY_RECORD"] = str(self.root / "missing" / "report.json")
        previous = git(self.qemu, "rev-parse", "HEAD")
        process = self.handler()
        self.assertNotEqual(0, process.returncode)
        self.assertEqual(previous, git(self.qemu, "rev-parse", "HEAD"))

    def test_final_report_publication_failure_returns_nonzero(self):
        # Permit the initial failed/incomplete record, then fail the final
        # publication. A successful Git operation must not hide that error.
        moves = self.root / "move-count"
        real_mv = shutil.which("mv")
        script = "#!" + sys.executable + "\n" + f'''from pathlib import Path
import subprocess, sys
count_file = Path({str(moves)!r})
count = int(count_file.read_text()) if count_file.exists() else 0
count_file.write_text(str(count + 1))
if count:
    sys.exit(1)
sys.exit(subprocess.call([{real_mv!r}] + sys.argv[1:]))
'''
        wrapper = self.bin / "mv"
        wrapper.write_text(script)
        wrapper.chmod(0o755)
        process = self.handler()
        self.assertNotEqual(0, process.returncode)
        self.assertIn(b"Change 1 - was applied", process.stdout)
        self.assertEqual("failed", json.loads(self.report_file.read_text())["status"])


if __name__ == "__main__":
    unittest.main()
