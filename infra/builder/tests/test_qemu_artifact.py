import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile
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

    def prepare(self, env=None):
        process = subprocess.Popen(
            ["bash", str(HELPER.with_name("prepare-qemu-vssim.sh")), str(self.qemu), str(self.source)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env,
        )
        stdout, stderr = process.communicate()
        return process.returncode, stdout, stderr

    def assert_prepared(self):
        status, stdout, stderr = self.prepare()
        self.assertEqual(0, status, (stdout, stderr))
        self.assertIn(b"INFO prepared", stdout)
        self.assertEqual([], list(self.vssim.glob(".simulator.*")))

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
