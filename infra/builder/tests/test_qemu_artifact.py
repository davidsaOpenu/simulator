import copy
from concurrent.futures import ThreadPoolExecutor
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


class IdentityTests(InputFixture):
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

    def test_environment_and_arguments_remain_literal(self):
        literal = "spaces; $(touch SHOULD_NOT_EXIST) `false`"
        self.request["build_environment"] = {"CFLAGS": literal}
        self.request["configure_args"].append("--extra-cflags=" + literal)
        result = self.build()
        args = result["commands"][1]["argv"]
        self.assertIn("CFLAGS=" + literal, args)
        self.assertIn("--extra-cflags=" + literal, args)
        self.assertFalse((self.root / "SHOULD_NOT_EXIST").exists())


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-publication-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.job = self.root / "job"
        self.qemu = self.job / "workspace/qemu"
        self.qemu.mkdir(parents=True)
        (self.job / "logs").mkdir()
        write(self.qemu, "VERSION", "11.0.0\n")
        self.inputs = {"container": IMAGE, "build_environment": {}, "source_fixture": "pinned"}
        self.identity = {"key": artifact.input_key(self.inputs), "inputs": self.inputs}
        self.result = {"status": "built", "identity": self.identity,
                       "qemu_dir": str(self.qemu), "commands": [{"exit_status": 0}]}
        self.artifacts = self.root / "artifacts"
        self.dist = self.root / "dist"
        self.installed_files = {"bin/qemu-system-x86_64": "executable", "bin/qemu-img": "tool",
                                "share/qemu/keymaps/en-us": "keymap"}
        self.installed_files.update({"share/qemu/" + name: "firmware " + name
                                     for name in artifact.REQUIRED_FIRMWARE})
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
            log.write_text("QEMU emulator version 11.0.0\n")
        else:
            self.assertIn("-L", argv)
            self.assertEqual(b"quit\n", input_data)
            self.assertEqual(30, timeout)
            self.assertIn("--interactive", argv)
            self.assertEqual("none", argv[argv.index("--network") + 1])
            log.write_text("paused PC initialized\n")

    def publish(self):
        return artifact.publish(self.result, self.artifacts, self.dist)

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
            self.assertEqual(set(self.installed_files), set(manifest["files"]))
            self.assertEqual("11.0.0", manifest["version"])
            self.assertTrue(os.access(folder / "bin/qemu-img", os.X_OK))
            self.assertFalse(any(p.is_symlink() for p in folder.rglob("*")))
        commands = json.loads((self.job / "logs/publication-commands.json").read_text())
        self.assertEqual(5, len(commands))
        self.assertFalse((self.job / "workspace/install").exists())

    def test_manifest_and_payload_corruption_are_rejected(self):
        selection = self.publish()
        original = Path(selection["bundle"])
        for mutation in ("content", "missing", "extra", "mode", "link", "inputs", "version"):
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
                    manifest[mutation] = {} if mutation == "inputs" else "2.12.0"
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
                self.assertEqual([], list(parent.iterdir()))
                self.assertFalse((self.job / "workspace/install").exists())

    def test_failed_install_and_payload_copy_leave_no_completed_bundle(self):
        with mock.patch.object(artifact, "run_logged", side_effect=artifact.InputError("install failed")):
            with self.assertRaisesRegex(artifact.InputError, "install failed"):
                self.publish()
        self.assertFalse(self.artifacts.exists())
        with mock.patch.object(artifact.shutil, "copytree", side_effect=OSError("copy failed")):
            with self.assertRaisesRegex(OSError, "copy failed"):
                self.publish()
        self.assertEqual([], list((self.artifacts / "qemu").iterdir()))
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
        self.assertEqual([], list((self.dist / "qemu").iterdir()))

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
