"""Artifact contracts with real Git, source preparation, and bundle files.

Docker/compiler execution is simulated at run_logged; source export, recipe
validation, manifests, checksums, atomic publication, and selection stay real.
These tests do not establish QEMU compilation or guest/runtime success.
"""

import contextlib
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import test_dependency_handler as dependency_tests


BUILDER = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("qemu_artifact", BUILDER / "qemu-artifact.py")
artifact = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifact)
SPEC = importlib.util.spec_from_file_location("qemu_inputs", BUILDER / "qemu-inputs.py")
resolver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resolver)
IMAGE = {"image_id": "sha256:" + "1" * 64, "os": "linux", "architecture": "amd64", "variant": ""}
COMMAND_OUTPUT = artifact.command_output


def without_docker_cleanup(arguments, cwd=None):
    if arguments[:3] == ["docker", "rm", "--force"]:
        return b""
    return COMMAND_OUTPUT(arguments, cwd)


def write(root, relative, content, executable=False):
    dependency_tests.write(root, relative, content)
    if executable:
        (root / relative).chmod(0o755)


class DockerBuild:
    """Supply controlled container outputs, retaining production orchestration."""

    def __init__(self, failure=None, werror=True):
        self.failure = failure
        self.werror = werror
        self.calls = []

    def __call__(self, arguments, log, commands, input_data=None, timeout=None):
        arguments = [str(arg) for arg in arguments]
        self.calls.append((log.name, arguments, input_data, timeout))
        command = {"argv": arguments, "log": str(log), "exit_status": 0}
        commands.append(command)
        log.write_text("controlled container output\n")
        if log.name == self.failure:
            command["exit_status"] = 7
            raise artifact.InputError("injected failure: " + log.name)
        workspace = log.parent.parent / "workspace"
        qemu = workspace / "qemu"
        if log.name == "prepare.log":
            process = subprocess.run(["bash", str(BUILDER / "prepare-qemu-vssim.sh"),
                str(qemu), str(workspace / "simulator/eVSSIM"),
                *(["--legacy"] if "--legacy" in arguments else [])], capture_output=True, text=True)
            log.write_text(process.stdout + process.stderr)
            if process.returncode:
                command["exit_status"] = process.returncode
                raise artifact.InputError("real source preparation failed: " + process.stdout)
        elif log.name == "configure.log":
            options = [{"name": "werror", "value": self.werror},
                       {"name": "prefix", "value": "/usr/local"},
                       {"name": "bindir", "value": "bin"},
                       {"name": "datadir", "value": "share"}]
            write(qemu, "build/meson-info/intro-buildoptions.json", json.dumps(options))
            write(qemu, "config-host.mak", "TOOLS=qemu-img$(EXESUF)\nEXESUF=\n"
                  "CONFIG_LINUX=y\nINSTALL_BLOBS=yes\n")
        elif log.name == "build.log":
            for name in ("build/qemu-system-x86_64", "x86_64-softmmu/qemu-system-x86_64",
                         "qemu-img", "qemu-bridge-helper"):
                write(qemu, name, "controlled executable payload\n", executable=True)
            write(qemu, "trace-events-all", "controlled traces\n")
        elif log.name == "install.log":
            payload = workspace / "install/usr/local"
            write(payload, "bin/qemu-system-x86_64", "controlled executable payload\n", executable=True)
            installed = {}
            for name in artifact.REQUIRED_FIRMWARE:
                relative = "share/qemu/" + name
                write(payload, relative, "controlled firmware " + name + "\n")
                installed[name] = "/usr/local/" + relative
            installed["binary"] = "/usr/local/bin/qemu-system-x86_64"
            write(qemu, "build/meson-info/intro-installed.json", json.dumps(installed))
        elif log.name.endswith("-version.log"):
            # Publication/cache probes still run their real version comparison.
            manifest = json.loads((Path(arguments[arguments.index("--volume") + 1]
                .removesuffix(":/artifact:ro")) / "manifest.json").read_text())
            log.write_text("QEMU emulator version " + manifest["version"] + "\n")


class ArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.templates = tempfile.TemporaryDirectory(prefix="qemu-artifact-templates-")
        cls.addClassCleanup(cls.templates.cleanup)
        root = Path(cls.templates.name)
        cls.cache = root / "subprojects"
        cls.cache.mkdir()
        cls.pins = {}
        for name in artifact.SUBPROJECTS:
            repo = cls.cache / name
            dependency_tests.new_repo(repo)
            write(repo, "source.c", name + " pinned source\n")
            cls.pins[name] = dependency_tests.commit(repo, "source.c")
        cls.simulator_template = root / "simulator"
        dependency_tests.new_repo(cls.simulator_template)
        # Use tracked authoritative sources, never an allowlist copied into tests.
        archive = subprocess.check_output(["git", "-C", str(BUILDER.parents[1]),
                                           "archive", "HEAD", "eVSSIM"])
        subprocess.run(["tar", "-xf", "-", "-C", str(cls.simulator_template)],
                       input=archive, check=True)
        for relative in artifact.RECIPE_FILES:
            destination = cls.simulator_template / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(BUILDER.parents[1] / relative, destination)
        dependency_tests.commit(cls.simulator_template, "eVSSIM", "infra")
        cls.qemu_template = root / "qemu"
        dependency_tests.new_repo(cls.qemu_template)
        write(cls.qemu_template, "VERSION", "11.0.0\n")
        write(cls.qemu_template, "hw/vssim/Makefile.objs", "obj-y += simulator/osd-schema.o\n")
        write(cls.qemu_template, "configure", 'symlink "$vssim_source/a" "$vssim_target/osd-schema.c"\n'
              'make -C "$vssim_target/osc-osd/osd-target" osd-schema.c\n', executable=True)
        write(cls.qemu_template, "Makefile", "BLOBS=" + " ".join(artifact.REQUIRED_FIRMWARE) + "\nKEYMAPS=en-us\n")
        for name in artifact.REQUIRED_FIRMWARE:
            write(cls.qemu_template, "pc-bios/" + name, "tracked firmware " + name + "\n")
        write(cls.qemu_template, "pc-bios/keymaps/en-us", "keymap\n")
        for name, pin in cls.pins.items():
            write(cls.qemu_template, "subprojects/" + name + ".wrap",
                  "[wrap-git]\nurl=https://unused.invalid/" + name + "\nrevision=" + pin +
                  "\npatch_directory=" + name + "\n")
            write(cls.qemu_template, "subprojects/packagefiles/" + name + "/meson.build", "# overlay\n")
        dependency_tests.commit(cls.qemu_template, "VERSION", "hw", "configure", "Makefile", "pc-bios", "subprojects")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qemu-artifact-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qemu, self.simulator = self.root / "qemu", self.root / "simulator"
        for template, repo in ((self.qemu_template, self.qemu), (self.simulator_template, self.simulator)):
            subprocess.run(["git", "clone", "-q", str(template), str(repo)], check=True)
            dependency_tests.git(repo, "config", "user.name", "Artifact test")
            dependency_tests.git(repo, "config", "user.email", "artifact@example.invalid")
            dependency_tests.git(repo, "config", "commit.gpgsign", "false")
        self.profiles = resolver.read_json(BUILDER / "versions/configs.json")
        self.policy = resolver.read_json(BUILDER / "versions/qemu-build-policy.json")
        self.report = {"schema_version": 1, "status": "complete", "dependencies": [self.dependency()]}
        image_patch = mock.patch.object(artifact, "inspect_image", return_value=copy.deepcopy(IMAGE))
        self.image = image_patch.start()
        self.addCleanup(image_patch.stop)
        self.request = self.resolve_request()
        self.identity = self.identify()
        self.artifacts, self.dist = self.root / "artifacts", self.root / "dist"

    def dependency(self, project="qemu", revision=None, branch="11.0"):
        repo = self.simulator if project == "simulator" else self.qemu
        return {"project": project, "url": "https://review.gerrithub.io/c/test/" + project + "/+/1",
                "patch_set": 7, "revision": "a" * 40, "ref": "refs/changes/01/1/7",
                "target_branch": branch, "applied_revision": revision or dependency_tests.git(repo, "rev-parse", "HEAD"),
                "disposition": "applied", "error": ""}

    def resolve_request(self):
        resolved = resolver.resolve_inputs(self.report, self.profiles, self.policy, self.qemu, self.simulator)
        return artifact.request_from_resolved(resolved, self.policy)

    def identify(self, request=None):
        return artifact.identify(request or self.request, str(self.qemu), str(self.simulator))

    def select(self, identity=None, job="job"):
        return artifact.select_artifact(identity or self.identity, str(self.qemu), str(self.simulator),
            str(self.root / job), 2, self.cache, self.artifacts, self.dist)

    def build(self, job="job"):
        return artifact.build(self.identity, str(self.qemu), str(self.simulator), str(self.root / job), 2, self.cache)

    def bundle(self, folder, identity=None):
        identity = identity or self.identity
        write(folder, "bin/qemu-system-x86_64", "controlled binary\n", executable=True)
        for name in artifact.REQUIRED_FIRMWARE:
            write(folder, "share/qemu/" + name, "firmware " + name + "\n")
        for name in artifact.REQUIRED_MONITOR_ASSETS:
            write(folder, artifact.VSSIM_WWW_DIR + "/" + name, "asset " + name + "\n")
        manifest = {"schema_version": 1, "key": identity["key"], "inputs": identity["inputs"],
            "version": identity["inputs"]["sources"]["qemu"]["version"],
            "executable": "bin/qemu-system-x86_64", "data_dir": "share/qemu",
            "vssim_www_dir": artifact.VSSIM_WWW_DIR, "files": artifact.file_inventory(folder),
            "build": "full-clean-passed"}
        write(folder, "manifest.json", json.dumps(manifest))
        return manifest

    def cli(self, *options):
        output, errors = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, "argv", [str(BUILDER / "qemu-artifact.py"), *map(str, options)]), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            status = artifact.main()
        return status, output.getvalue(), errors.getvalue()

    def test_canonical_key_matches_independent_digest_and_ignores_object_order(self):
        expected = hashlib.sha256(json.dumps(self.identity["inputs"], sort_keys=True,
            separators=(",", ":"), ensure_ascii=True, allow_nan=False).encode("ascii")).hexdigest()
        self.assertEqual(expected, self.identity["key"])
        self.assertEqual(expected, artifact.input_key(dict(reversed(list(self.identity["inputs"].items())))))

    def test_actual_key_inputs_invalidate_identity(self):
        # Exercise identify(), not just hashing a manually altered identity.
        for field, value in (("target", "aarch64-softmmu"), ("configure_args", ["--changed"]),
                             ("build_environment", {"CFLAGS": "-O1"})):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.request)
                changed[field] = value
                self.assertNotEqual(self.identity["key"], self.identify(changed)["key"])
        changed = copy.deepcopy(self.request)
        changed["profile"]["guestTests"]["guestVmImage"] += "-changed"
        self.assertNotEqual(self.identity["key"], self.identify(changed)["key"])
        self.image.return_value = dict(IMAGE, image_id="sha256:" + "2" * 64)
        self.assertNotEqual(self.identity["key"], self.identify()["key"])
        self.image.return_value = IMAGE
        for name, repo in (("qemu", self.qemu), ("simulator", self.simulator)):
            with self.subTest(source=name):
                write(repo, "new-source", name)
                changed = copy.deepcopy(self.request)
                changed["sources"][name]["revision"] = dependency_tests.commit(repo, "new-source")
                self.assertNotEqual(self.identity["key"], self.identify(changed)["key"])
                self.request = changed

    def test_recipe_and_gitlink_changes_invalidate_identity(self):
        write(self.simulator, "infra/builder/builder.sh", "# changed recipe\n")
        self.request["sources"]["simulator"]["revision"] = dependency_tests.commit(
            self.simulator, "infra/builder/builder.sh")
        changed = self.identify()
        self.assertNotEqual(self.identity["inputs"]["recipes"], changed["inputs"]["recipes"])
        self.assertNotEqual(self.identity["key"], changed["key"])
        subprocess.run(["git", "clone", "-q", str(self.cache / "dtc"), str(self.qemu / "dtc")], check=True)
        dependency_tests.git(self.qemu, "update-index", "--add", "--cacheinfo",
                             "160000," + self.pins["dtc"] + ",dtc")
        dependency_tests.git(self.qemu, "commit", "-qm", "record gitlink")
        self.request["sources"]["qemu"]["revision"] = dependency_tests.git(self.qemu, "rev-parse", "HEAD")
        linked = self.identify()
        self.assertEqual(self.pins["dtc"], linked["inputs"]["sources"]["qemu"]["submodules"]["dtc"])
        self.assertNotEqual(changed["key"], linked["key"])

    def test_source_head_dirty_recipe_and_unapplied_dependency_are_rejected(self):
        changed = copy.deepcopy(self.request)
        changed["sources"]["qemu"]["revision"] = "f" * 40
        with self.assertRaisesRegex(artifact.InputError, "HEAD differs"):
            self.identify(changed)
        write(self.qemu, "VERSION", "dirty\n")
        with self.assertRaisesRegex(artifact.InputError, "tracked source"):
            self.identify()
        write(self.qemu, "VERSION", "11.0.0\n")
        changed = copy.deepcopy(self.request)
        changed.pop("dependency_report")
        changed["dependencies"][0]["applied_revision"] = "f" * 40
        with self.assertRaisesRegex(artifact.InputError, "not reachable"):
            self.identify(changed)
        write(self.simulator, "infra/builder/qemu-artifact.py", "different helper\n")
        self.request["sources"]["simulator"]["revision"] = dependency_tests.commit(
            self.simulator, "infra/builder/qemu-artifact.py")
        with self.assertRaisesRegex(artifact.InputError, "helper differs"):
            self.identify()

    def test_non_qemu_dependencies_keep_provenance_without_invalidating_bundle(self):
        for project in ("kernel", "nvme-cli", "tnvme", "dnvme", "open-osd"):
            self.report["dependencies"].append(self.dependency(project, branch="master"))
        self.report["dependencies"].append(self.dependency("simulator", branch="master"))
        request = self.resolve_request()
        identity = self.identify(request)
        for index in range(1, 6):
            with self.subTest(project=self.report["dependencies"][index]["project"]):
                changed = copy.deepcopy(self.report)
                changed["dependencies"][index].update(revision="b" * 40, applied_revision="c" * 40)
                self.report, original = changed, self.report
                current = self.identify(self.resolve_request())
                self.report = original
                self.assertEqual(identity["key"], current["key"])
                self.assertEqual(changed["dependencies"], current["dependency_report"])
        request["dependencies"][0]["revision"] = "d" * 40
        request["dependency_report"][0]["revision"] = "d" * 40
        self.assertNotEqual(identity["key"], self.identify(request)["key"])
        failed = copy.deepcopy(identity["dependency_report"])
        failed[1]["disposition"] = "failed"
        with self.assertRaises(artifact.InputError):
            artifact.artifact_dependencies(failed)

    def test_s2a_report_reaches_s2b_and_artifact_identity(self):
        fixture = dependency_tests.DependencyHandlerTests("test_real_cherry_pick_records_declared_and_applied_revisions")
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        process = fixture.handler()
        self.assertEqual(0, process.returncode, process.stderr.decode())
        report = json.loads(fixture.report_file.read_text())
        # Add a tracked VERSION after the real handler's cherry-pick; applied
        # dependency must remain an ancestor, not necessarily the final HEAD.
        write(fixture.qemu, "VERSION", "11.0.0\n")
        dependency_tests.commit(fixture.qemu, "VERSION")
        resolved = resolver.resolve_inputs(report, self.profiles, self.policy, fixture.qemu, self.simulator)
        request = artifact.request_from_resolved(resolved, self.policy)
        identity = artifact.identify(request, str(fixture.qemu), str(self.simulator))
        self.assertEqual(resolved["profile"], identity["inputs"]["profile"])
        self.assertEqual(4, identity["inputs"]["profile"]["id"])
        self.assertEqual(report["dependencies"], identity["dependency_report"])
        self.assertEqual(report["dependencies"][0]["applied_revision"],
                         identity["inputs"]["dependencies"][0]["applied_revision"])
        for name in ("qemu", "simulator"):
            self.assertEqual(resolved["sources"][name]["revision"], identity["inputs"]["sources"][name]["revision"])
        report["status"] = "failed"
        with mock.patch.object(artifact, "identify") as identify, self.assertRaises(artifact.InputError):
            artifact.resolve_request(report, self.profiles, self.policy, fixture.qemu, self.simulator)
        identify.assert_not_called()

    def test_saved_record_round_trip_and_environment_conflicts(self):
        current = artifact.validate_saved_inputs(self.identity, str(self.qemu), str(self.simulator))
        self.assertEqual(self.identity["inputs"], current["inputs"])
        self.assertEqual(self.identity["dependency_report"], current["dependency_report"])
        self.assertEqual(self.request["profile"], artifact.profile_from_inputs(self.identity))
        self.assertEqual(IMAGE["image_id"], self.image.call_args.args[0])
        with self.assertRaisesRegex(artifact.InputError, "environment conflicts"):
            artifact.validate_saved_inputs(self.identity, str(self.qemu), str(self.simulator), {})
        altered = copy.deepcopy(self.identity)
        altered["inputs"]["target"] = "changed"
        with self.assertRaisesRegex(artifact.InputError, "key/schema"):
            artifact.request_from_inputs(altered)
        self.image.return_value = dict(IMAGE, image_id="sha256:" + "2" * 64)
        with self.assertRaisesRegex(artifact.InputError, "container differ"):
            artifact.validate_saved_inputs(self.identity, str(self.qemu), str(self.simulator))

    def test_ordinary_and_artifact_configure_arguments_match_both_lanes(self):
        for container in ("ubuntu-14.04", "ubuntu-26.04"):
            with self.subTest(container=container):
                status, output, errors = self.cli("--configure-command", container)
                self.assertEqual(0, status, errors)
                recipe = artifact.build_recipe(self.policy, container)
                args = shlex.split(output)
                expected = ["./configure", "--target-list=x86_64-softmmu", *recipe["configure_args"]]
                self.assertEqual(expected, args)
                self.assertEqual(expected, artifact.configure_argv(recipe))
                self.assertEqual(["--target-list=x86_64-softmmu"],
                                 [arg for arg in args if arg.startswith("--target-list")])
                if container == "ubuntu-26.04":
                    self.assertIn("--enable-werror", args)
                    self.assertIn("--disable-download", args)
                else:
                    self.assertIn("--enable-vssim", args)
                    self.assertEqual(1, sum(arg.startswith("--extra-cflags=") for arg in args))
        bad = copy.deepcopy(recipe)
        bad["configure_args"].append("--target-list=other")
        with self.assertRaisesRegex(artifact.InputError, "target separately"):
            artifact.validate_recipe(bad)

    def prepare(self, legacy=False):
        return subprocess.run(["bash", str(BUILDER / "prepare-qemu-vssim.sh"), str(self.qemu),
            str(self.simulator / "eVSSIM"), *(["--legacy"] if legacy else [])],
            capture_output=True, text=True, timeout=15)

    def test_real_preparation_copies_regular_sources_schema_assets_and_refreshes(self):
        self.assertEqual(0, self.prepare().returncode)
        view = self.qemu / "hw/vssim/simulator"
        self.assertFalse(any(path.is_symlink() for path in view.rglob("*")))
        self.assertEqual((self.simulator / "eVSSIM/SSD_MODULE/onfi.c").read_bytes(), (view / "onfi.c").read_bytes())
        self.assertTrue((view / "osc-osd/osd-util/osd-defs.h").is_file())
        schema = (self.simulator / "eVSSIM/osc-osd/osd-target/osd.schema").read_text()
        self.assertEqual('const char osd_schema[] =\n' +
            ''.join('"' + line + '\\n"\n' for line in schema.splitlines()) + ';\n',
            (view / "osd-schema.c").read_text())
        for name in artifact.REQUIRED_MONITOR_ASSETS:
            self.assertEqual((self.simulator / "eVSSIM/MONITOR/SERVER/www" / name).read_bytes(),
                             (view / "www" / name).read_bytes())
        write(self.simulator, "eVSSIM/SSD_MODULE/onfi.c", "refreshed authoritative source\n")
        write(view, "stale-generated-output", "old\n")
        self.assertEqual(0, self.prepare().returncode)
        self.assertEqual("refreshed authoritative source\n", (view / "onfi.c").read_text())
        self.assertFalse((view / "stale-generated-output").exists())
        self.assertFalse((view / "osc-osd/tgt").exists())

    def test_preparation_failure_preserves_old_view_and_removes_partial_staging(self):
        self.assertEqual(0, self.prepare().returncode)
        view = self.qemu / "hw/vssim/simulator"
        before = artifact.file_inventory(view)
        source = self.simulator / "eVSSIM/SSD_MODULE/onfi.c"
        content = source.read_bytes()
        for failure in ("missing", "linked-file", "linked-parent"):
            with self.subTest(failure=failure):
                source.unlink()
                if failure == "linked-file":
                    source.symlink_to(self.qemu / "VERSION")
                elif failure == "linked-parent":
                    source.write_bytes(content)
                    source.parent.rename(source.parent.with_name("original-SSD_MODULE"))
                    source.parent.symlink_to("original-SSD_MODULE", target_is_directory=True)
                process = self.prepare()
                self.assertNotEqual(0, process.returncode)
                self.assertEqual(before, artifact.file_inventory(view))
                self.assertEqual([], list((self.qemu / "hw/vssim").glob(".simulator.*")))
                if failure == "linked-parent":
                    source.parent.unlink()
                    source.parent.with_name("original-SSD_MODULE").rename(source.parent)
                else:
                    if source.is_symlink():
                        source.unlink()
                    source.write_bytes(content)

    def test_legacy_preparation_and_configure_use_regular_files(self):
        process = self.prepare(legacy=True)
        self.assertEqual(0, process.returncode, process.stdout)
        view = self.qemu / "hw/vssim/simulator"
        self.assertEqual((view / "osd-schema.c").read_bytes(),
                         (view / "osc-osd/osd-target/osd-schema.c").read_bytes())
        result = artifact.prepare_legacy_configure(self.qemu)
        self.assertNotEqual(result["original_configure_sha256"], result["prepared_configure_sha256"])
        self.assertNotIn('symlink "$vssim_source/', (self.qemu / "configure").read_text())
        write(self.qemu, "hw/vssim/Makefile.objs", "obj-y += simulator/not-prepared.o\n")
        with self.assertRaisesRegex(artifact.InputError, "missing simulator sources"):
            artifact.prepare_legacy_configure(self.qemu)

    def test_pinned_subprojects_export_commits_and_overlays_without_generated_files(self):
        exported = self.root / "exported"
        artifact.export_tree(str(self.qemu), self.request["sources"]["qemu"]["revision"], exported)
        self.assertEqual(self.pins, artifact.prepare_subprojects(exported, self.cache))
        for name in artifact.SUBPROJECTS:
            destination = exported / "subprojects" / name
            self.assertEqual(name + " pinned source\n", (destination / "source.c").read_text())
            self.assertEqual("# overlay\n", (destination / "meson.build").read_text())
            self.assertFalse((destination / ".git").exists())
        wrap = exported / "subprojects/dtc.wrap"
        wrap.write_text(wrap.read_text().replace(self.pins["dtc"], "not-a-pin"))
        with self.assertRaisesRegex(artifact.InputError, "full lowercase Git SHA"):
            artifact.prepare_subprojects(exported, self.cache)

    def test_ordinary_subprojects_prepare_before_verifying_exact_pins(self):
        for name in artifact.SUBPROJECTS:
            subprocess.run(["git", "clone", "-q", str(self.cache / name),
                            str(self.qemu / "subprojects" / name)], check=True)
        real = artifact.command_output
        with mock.patch.object(artifact, "command_output") as command:
            # Only Meson transport is simulated; pin verification/export is real.
            command.side_effect = lambda args, cwd=None: b"" if args[0] == "meson" else real(args, cwd)
            self.assertEqual(self.pins, artifact.prepare_subprojects(self.qemu))
            meson_calls = [c.args[0] for c in command.call_args_list if c.args[0][0] == "meson"]
            self.assertEqual([["meson", "subprojects", "download", *artifact.SUBPROJECTS],
                              ["meson", "subprojects", "update", "--reset", *artifact.SUBPROJECTS]], meson_calls)
        write(self.qemu / "subprojects/dtc", "source.c", "wrong revision\n")
        dependency_tests.git(self.qemu / "subprojects/dtc", "config", "user.name", "Artifact test")
        dependency_tests.git(self.qemu / "subprojects/dtc", "config", "user.email", "artifact@example.invalid")
        dependency_tests.git(self.qemu / "subprojects/dtc", "config", "commit.gpgsign", "false")
        dependency_tests.commit(self.qemu / "subprojects/dtc", "source.c")
        with mock.patch.object(artifact, "command_output", side_effect=lambda args, cwd=None:
                               b"" if args[0] == "meson" else real(args, cwd)):
            with self.assertRaisesRegex(artifact.InputError, "HEAD differs from wrap pin"):
                artifact.prepare_subprojects(self.qemu)

    def test_build_exports_clean_sources_and_runs_only_qemu_recipe_in_order(self):
        write(self.qemu, "untracked-build-output", "must not be exported\n")
        docker = DockerBuild()
        with mock.patch.object(artifact, "run_logged", side_effect=docker):
            result = self.build()
        qemu = Path(result["qemu_dir"])
        self.assertFalse((qemu / "untracked-build-output").exists())
        self.assertFalse((qemu / ".git").exists())
        self.assertEqual(["prepare.log", "configure.log", "build.log", "version.log"],
                         [name for name, *_ in docker.calls])
        configure = docker.calls[1][1]
        self.assertEqual(artifact.configure_argv(self.identity["inputs"]), configure[configure.index("./configure"):])
        self.assertIn(IMAGE["image_id"], configure)
        self.assertNotIn(self.request["container_image"], configure)
        self.assertTrue((qemu / "hw/vssim/simulator/onfi.c").is_file())
        self.assertTrue((self.root / "job/build-result.json").is_file())
        self.assertEqual(self.pins, result["subprojects"])
        self.assertEqual("built", result["status"])
        self.assertEqual("", dependency_tests.git(self.qemu, "status", "--porcelain", "--untracked-files=no"))

    def test_build_failure_at_each_container_stage_retains_logs_without_success(self):
        for stage in ("prepare.log", "configure.log", "build.log", "version.log"):
            with self.subTest(stage=stage):
                job = "failed-" + stage
                docker = DockerBuild(failure=stage)
                with mock.patch.object(artifact, "run_logged", side_effect=docker), self.assertRaises(artifact.InputError):
                    self.build(job)
                folder = self.root / job
                self.assertFalse((folder / "workspace").exists())
                self.assertFalse((folder / "build-result.json").exists())
                commands = json.loads((folder / "logs/commands.json").read_text())
                self.assertEqual(7, commands[-1]["exit_status"])
                self.assertEqual(stage, Path(commands[-1]["log"]).name)
        self.assertFalse(self.artifacts.exists())

    def test_missing_pinned_sources_and_disabled_werror_stop_before_compile(self):
        with mock.patch.object(artifact, "run_logged") as run, self.assertRaises(artifact.InputError):
            artifact.build(self.identity, str(self.qemu), str(self.simulator),
                           str(self.root / "missing-pins"), 2, self.root / "missing-cache")
        run.assert_not_called()
        self.assertFalse((self.root / "missing-pins/workspace").exists())
        docker = DockerBuild(werror=False)
        with mock.patch.object(artifact, "run_logged", side_effect=docker), self.assertRaisesRegex(
                artifact.InputError, "werror must be true"):
            self.build("no-werror")
        self.assertEqual(["prepare.log", "configure.log"], [call[0] for call in docker.calls])
        self.assertFalse((self.root / "no-werror/workspace").exists())

    def test_unsafe_build_options_fail_before_creating_job(self):
        for changes in ({"target": "other"}, {"configure_args": ["--disable-download"]},
                        {"configure_args": ["--enable-werror", "--disable-download", "--disable-werror"]}):
            with self.subTest(changes=changes):
                identity = copy.deepcopy(self.identity)
                identity["inputs"].update(changes)
                with self.assertRaises(artifact.InputError):
                    self.select(identity)
                self.assertFalse((self.root / "job").exists())
        for jobs in (0, -1, True):
            with self.subTest(jobs=jobs), self.assertRaises(artifact.InputError):
                artifact.validate_build(self.identity, jobs)

    def test_bundle_verifies_inventory_version_layout_and_required_assets(self):
        folder = self.root / "bundle"
        manifest = self.bundle(folder)
        self.assertEqual(manifest, artifact.verify_bundle(folder, self.identity))
        for name in (*artifact.REQUIRED_FIRMWARE, *artifact.REQUIRED_MONITOR_ASSETS):
            with self.subTest(asset=name):
                relative = "share/qemu/" + name if name in artifact.REQUIRED_FIRMWARE else artifact.VSSIM_WWW_DIR + "/" + name
                path = folder / relative
                data = path.read_bytes()
                for missing in (True, False):
                    path.unlink() if missing else path.write_bytes(b"")
                    # Rehash to test required-asset checks independently of checksum rejection.
                    changed = dict(manifest, files=artifact.file_inventory(folder))
                    write(folder, "manifest.json", json.dumps(changed))
                    with self.assertRaises(artifact.InputError):
                        artifact.verify_bundle(folder, self.identity)
                    path.write_bytes(data)
        write(folder, "manifest.json", json.dumps(manifest))
        for field, value in (("version", "11.0.9"), ("key", "f" * 64),
                             ("build", "partial"), ("executable", "../escape"),
                             ("vssim_www_dir", "wrong-layout")):
            with self.subTest(field=field):
                write(folder, "manifest.json", json.dumps(dict(manifest, **{field: value})))
                with self.assertRaises(artifact.InputError):
                    artifact.verify_bundle(folder, self.identity)

    def test_corrupt_payload_and_non_regular_files_are_rejected(self):
        folder = self.root / "bundle"
        manifest = self.bundle(folder)
        binary = folder / manifest["executable"]
        for change in ("content", "mode", "missing", "symlink"):
            with self.subTest(change=change):
                if change == "content":
                    binary.write_text("corrupt\n")
                elif change == "mode":
                    binary.chmod(0o644)
                elif change == "missing":
                    binary.unlink()
                else:
                    binary.unlink()
                    binary.symlink_to(self.qemu / "VERSION")
                with self.assertRaises(artifact.InputError):
                    artifact.verify_bundle(folder, self.identity)
                if binary.is_symlink():
                    binary.unlink()
                write(folder, manifest["executable"], "controlled binary\n", executable=True)
        os.mkfifo(folder / "fifo")
        with self.assertRaisesRegex(artifact.InputError, "non-regular"):
            artifact.file_inventory(folder)

    def test_atomic_publication_cleans_candidates_on_copy_or_probe_failure(self):
        parent = self.root / "bundles"
        logs = self.root / "logs"
        logs.mkdir()
        def fail_copy(candidate):
            write(candidate, "partial", "incomplete\n")
            raise OSError("copy failed")
        for populate, probe in ((fail_copy, None), (self.bundle, artifact.InputError("probe failed"))):
            with self.subTest(probe=probe), mock.patch.object(artifact, "probe_bundle", side_effect=probe):
                with self.assertRaises((OSError, artifact.InputError)):
                    artifact.atomic_bundle(parent, self.identity, populate, logs, [], "publish")
                self.assertFalse((parent / self.identity["key"]).exists())
                self.assertEqual([], [p for p in parent.iterdir() if p.is_dir()])

    def test_atomic_publication_keeps_concurrent_winner_and_rejects_invalid_existing_entry(self):
        parent = self.root / "bundles"
        logs = self.root / "logs"
        logs.mkdir()
        final = parent / self.identity["key"]
        def concurrent(candidate):
            self.bundle(candidate)
            self.bundle(final)
            write(final, "winner", "concurrent payload\n")
            manifest = json.loads((final / "manifest.json").read_text())
            manifest["files"] = artifact.file_inventory(final)
            write(final, "manifest.json", json.dumps(manifest))
        with mock.patch.object(artifact, "probe_bundle"):
            artifact.atomic_bundle(parent, self.identity, concurrent, logs, [], "publish")
            self.assertTrue((final / "winner").exists())
            self.assertEqual([], [p for p in parent.iterdir() if p.is_dir() and p != final])
            write(final, "winner", "corrupt winner\n")
            with mock.patch.object(shutil, "copytree") as copytree, self.assertRaises(artifact.InputError):
                artifact.atomic_bundle(parent, self.identity, self.bundle, logs, [], "publish")
            copytree.assert_not_called()
            self.assertEqual("corrupt winner\n", (final / "winner").read_text())

    def test_miss_publishes_complete_bundle_and_hit_skips_configure_compile_install(self):
        docker = DockerBuild()
        with mock.patch.object(artifact, "run_logged", side_effect=docker):
            selected = self.select()
            self.assertEqual(("selected", "miss"), (selected["status"], selected["cache"]))
            self.assertEqual(self.identity["dependency_report"], selected["dependency_report"])
            persistent = Path(selected["bundle"])
            self.assertEqual(artifact.file_inventory(persistent), artifact.file_inventory(Path(selected["staged_bundle"])))
            self.assertEqual("selected", json.loads((self.root / "job/logs/cache.json").read_text())["status"])
            install_calls = [args for name, args, *_ in docker.calls if name == "install.log"]
            self.assertEqual(1, len(install_calls))
            self.assertIn("--no-rebuild", install_calls[0])
            self.assertFalse((self.root / "job/workspace/install").exists())
            shutil.rmtree(self.dist)
            docker.calls.clear()
            with mock.patch.object(artifact, "build", side_effect=AssertionError("cache hit rebuilt")), \
                    mock.patch.object(artifact, "publish", side_effect=AssertionError("cache hit installed")):
                reused = self.select(job="reuse")
            self.assertEqual("hit", reused["cache"])
            self.assertEqual(selected["binary_sha256"], reused["binary_sha256"])
            self.assertTrue(all(name.startswith("cache") for name, *_ in docker.calls))
            self.assertEqual("selected", json.loads((self.root / "reuse/logs/cache.json").read_text())["status"])

    def test_changed_inputs_miss_and_keep_previous_bundle(self):
        with mock.patch.object(artifact, "run_logged", side_effect=DockerBuild()):
            first = self.select()
            request = copy.deepcopy(self.request)
            request["build_environment"] = {"CFLAGS": "-O1"}
            changed = self.identify(request)
            second = self.select(changed, "changed")
        self.assertEqual("miss", second["cache"])
        self.assertNotEqual(first["key"], second["key"])
        self.assertTrue(Path(first["executable"]).is_file())
        self.assertTrue(Path(second["executable"]).is_file())
        self.assertNotEqual(first["bundle"], second["bundle"])

    def test_corrupt_cache_is_rejected_without_rebuild_or_success_record(self):
        persistent = self.artifacts / "qemu" / self.identity["key"]
        self.bundle(persistent)
        write(persistent, "bin/qemu-system-x86_64", "corrupt\n", executable=True)
        with mock.patch.object(artifact, "build") as build, mock.patch.object(artifact, "run_logged") as run:
            with self.assertRaisesRegex(artifact.InputError, "inventory/checksums"):
                self.select()
        build.assert_not_called()
        run.assert_not_called()
        self.assertEqual("rejected", json.loads((self.root / "job/logs/cache.json").read_text())["status"])
        self.assertFalse(self.dist.exists())
        self.assertFalse((self.root / "job/build-result.json").exists())

    def test_publication_install_or_probe_failure_never_exposes_partial_bundle(self):
        for stage in ("install.log", "publish-version.log", "publish-firmware.log"):
            with self.subTest(stage=stage):
                docker = DockerBuild(failure=stage)
                job = "failure-" + stage
                with mock.patch.object(artifact, "run_logged", side_effect=docker), \
                        mock.patch.object(artifact, "command_output", side_effect=without_docker_cleanup):
                    with self.assertRaises(artifact.InputError):
                        self.select(job=job)
                self.assertFalse((self.artifacts / "qemu" / self.identity["key"]).exists())
                self.assertFalse((self.dist / "qemu" / self.identity["key"]).exists())
                self.assertFalse((self.root / job / "workspace/install").exists())
                self.assertEqual("failed", json.loads((self.root / job / "logs/cache.json").read_text())["status"])
                if (self.artifacts / "qemu").exists():
                    self.assertEqual([], [p for p in (self.artifacts / "qemu").iterdir() if p.is_dir()])

    def test_staging_failure_keeps_verified_persistent_bundle_but_no_selected_result(self):
        docker = DockerBuild(failure="stage-firmware.log")
        with mock.patch.object(artifact, "run_logged", side_effect=docker), \
                mock.patch.object(artifact, "command_output", side_effect=without_docker_cleanup), self.assertRaises(artifact.InputError):
            self.select()
        artifact.verify_bundle(self.artifacts / "qemu" / self.identity["key"], self.identity)
        self.assertFalse((self.dist / "qemu" / self.identity["key"]).exists())
        self.assertEqual([], [p for p in (self.dist / "qemu").iterdir() if p.is_dir()])
        self.assertEqual("failed", json.loads((self.root / "job/logs/cache.json").read_text())["status"])

    def test_probe_uses_packaged_firmware_and_cleans_timed_out_container(self):
        folder = self.root / "bundle"
        manifest = self.bundle(folder)
        logs = self.root / "logs"
        logs.mkdir()
        docker = DockerBuild()
        with mock.patch.object(artifact, "run_logged", side_effect=docker):
            artifact.probe_bundle(folder, self.identity, manifest, logs, [], "test")
        _, args, stdin, timeout = docker.calls[-1]
        self.assertIn(str(folder) + ":/artifact:ro", args)
        self.assertEqual("none", args[args.index("--network") + 1])
        self.assertEqual("/artifact/share/qemu", args[args.index("-L") + 1])
        self.assertEqual("pc,accel=tcg", args[args.index("-machine") + 1])
        self.assertEqual((b"quit\n", 30), (stdin, timeout))
        with mock.patch.object(artifact, "run_logged", side_effect=artifact.InputError("timeout")), \
                mock.patch.object(artifact, "command_output", return_value=b"") as cleanup, \
                self.assertRaises(artifact.InputError):
            artifact.probe_bundle(folder, self.identity, manifest, logs, [], "test")
        self.assertEqual(["docker", "rm", "--force"], cleanup.call_args.args[0][:3])

    def test_missing_or_duplicate_manifest_and_wrong_binary_version_are_rejected(self):
        folder = self.root / "bundle"
        manifest = self.bundle(folder)
        path = folder / "manifest.json"
        path.unlink()
        with self.assertRaises(OSError):
            artifact.verify_bundle(folder, self.identity)
        path.write_text('{"key":1,"key":2}')
        with self.assertRaisesRegex(artifact.InputError, "duplicate JSON field"):
            artifact.verify_bundle(folder, self.identity)
        path.write_text(json.dumps(manifest))
        logs = self.root / "logs"
        logs.mkdir()
        def wrong_version(arguments, log, commands, **kwargs):
            log.write_text("QEMU emulator version 2.12.1\n")
        with mock.patch.object(artifact, "run_logged", side_effect=wrong_version) as run, \
                mock.patch.object(artifact, "command_output", side_effect=without_docker_cleanup), \
                self.assertRaisesRegex(artifact.InputError, "version does not match"):
            artifact.probe_bundle(folder, self.identity, manifest, logs, [], "wrong")
        self.assertEqual(1, run.call_count)

    def test_legacy_build_and_publication_coexist_with_modern(self):
        with mock.patch.object(artifact, "run_logged", side_effect=DockerBuild()):
            modern = self.select()
        for relative in artifact.LEGACY_SUBMODULES:
            repo = self.qemu / relative
            subprocess.run(["git", "clone", "-q", str(self.cache / "dtc"), str(repo)], check=True)
            dependency_tests.git(self.qemu, "update-index", "--add", "--cacheinfo",
                                 "160000," + self.pins["dtc"] + "," + relative)
        write(self.qemu, "VERSION", "2.12.1\n")
        dependency_tests.commit(self.qemu, "VERSION")
        self.report["dependencies"] = []
        self.request = self.resolve_request()
        legacy = self.identify()
        docker = DockerBuild()
        with mock.patch.object(artifact, "run_logged", side_effect=docker):
            selected = artifact.select_artifact(legacy, str(self.qemu), str(self.simulator),
                str(self.root / "legacy"), 2, None, self.artifacts, self.dist)
        self.assertEqual(2, selected["profile_id"])
        self.assertEqual("2.12.1", selected["version"])
        self.assertNotEqual(modern["bundle"], selected["bundle"])
        self.assertTrue(Path(modern["executable"]).is_file())
        self.assertTrue(Path(selected["executable"]).is_file())
        names = [name for name, *_ in docker.calls]
        self.assertEqual(["prepare.log", "configure.log", "clean.log", "build.log", "version.log"], names[:5])
        self.assertNotIn("install.log", names)
        payload = Path(selected["staged_bundle"])
        self.assertTrue((payload / "bin/qemu-img").is_file())
        self.assertTrue((payload / "share/qemu/keymaps/en-us").is_file())
        self.assertFalse(any(p.is_symlink() for p in payload.rglob("*")))

    def test_cli_selection_failures_return_nonzero_and_no_success_json(self):
        request = self.root / "request.json"
        request.write_text(json.dumps(self.request))
        for stage in ("prepare.log", "configure.log", "build.log", "install.log", "stage-firmware.log"):
            with self.subTest(stage=stage), mock.patch.object(artifact, "run_logged", side_effect=DockerBuild(failure=stage)), \
                    mock.patch.object(artifact, "command_output", side_effect=without_docker_cleanup):
                status, output, errors = self.cli("--request", request,
                    "--qemu-dir", self.qemu, "--simulator-dir", self.simulator,
                    "--subproject-cache", self.cache, "--build-dir", self.root / ("cli-" + stage),
                    "--artifact-root", self.artifacts, "--dist-root", self.dist)
                self.assertEqual(1, status, errors)
                self.assertEqual("", output)
                self.assertIn("ERROR:", errors)

    def test_cli_resolved_and_saved_modes_preserve_frozen_selection(self):
        resolved = resolver.resolve_inputs(self.report, self.profiles, self.policy, self.qemu, self.simulator)
        path = self.root / "resolved.json"
        path.write_text(json.dumps(resolved))
        args = ["--qemu-dir", self.qemu, "--simulator-dir", self.simulator]
        status, output, errors = self.cli("--resolved-inputs", path, *args)
        self.assertEqual(0, status, errors)
        record = json.loads(output)
        self.assertEqual(self.identity, record)
        path.write_text(json.dumps(record))
        status, output, errors = self.cli("--saved-inputs", path, *args)
        self.assertEqual(0, status, errors)
        self.assertEqual(record["inputs"], json.loads(output)["inputs"])
        status, output, _ = self.cli("--saved-inputs", path, "--profile-id", "2", *args)
        self.assertEqual((1, ""), (status, output))
        path.write_text('{"key":1,"key":2}')
        self.assertEqual((1, ""), self.cli("--saved-inputs", path, *args)[:2])

    def test_cli_failed_report_stops_before_artifact_lookup(self):
        report = self.root / "failed-report.json"
        report.write_text(json.dumps(dict(self.report, status="failed")))
        self.image.reset_mock()
        with mock.patch.object(artifact, "select_artifact") as select:
            status, output, errors = self.cli("--resolve-dependencies", report,
                "--qemu-dir", self.qemu, "--simulator-dir", self.simulator,
                "--build-dir", self.root / "job", "--artifact-root", self.artifacts, "--dist-root", self.dist)
        self.assertEqual((1, ""), (status, output))
        self.assertIn("ERROR:", errors)
        self.image.assert_not_called()
        select.assert_not_called()
        self.assertFalse((self.root / "job").exists())

    def test_run_logged_reports_real_exit_status_and_timeout(self):
        commands = []
        with self.assertRaisesRegex(artifact.InputError, "exit 7"):
            artifact.run_logged([sys.executable, "-c", "print('failure'); raise SystemExit(7)"],
                                self.root / "failure.log", commands)
        self.assertEqual(7, commands[-1]["exit_status"])
        self.assertIn("failure", (self.root / "failure.log").read_text())
        with self.assertRaisesRegex(artifact.InputError, "timed out"):
            artifact.run_logged([sys.executable, "-c", "import time; time.sleep(30)"],
                                self.root / "timeout.log", commands, timeout=0.05)
        self.assertEqual("timeout", commands[-1]["exit_status"])
