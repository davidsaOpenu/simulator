import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
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


class IdentityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-artifact-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu = self.root / "qemu"
        self.simulator = self.root / "simulator"
        for repo in (self.qemu, self.simulator):
            new_repo(repo)
        write(self.qemu, "source.c", "qemu input\n")
        qemu_revision = commit(self.qemu, "source.c")
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

    def test_complete_input_record_and_relative_handoff(self):
        result = self.identify()
        inputs = result["inputs"]
        self.assertEqual(1, inputs["schema_version"])
        self.assertEqual(self.request["profile"], inputs["profile"])
        self.assertEqual(self.request["dependencies"], inputs["dependencies"])
        self.assertEqual(IMAGE, inputs["container"])
        self.assertEqual("x86_64-softmmu", inputs["target"])
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
