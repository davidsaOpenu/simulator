#!/usr/bin/env python3
"""Identify QEMU inputs and optionally perform an isolated full clean build."""

import argparse
import configparser
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import uuid


SCHEMA_VERSION = 1
RECIPE_FILES = (
    "infra/builder/qemu-artifact.py",
    "infra/builder/compile-qemu.sh",
    "infra/builder/prepare-qemu-vssim.sh",
    "infra/builder/builder.sh",
)
# The QEMU 11 x86 build uses these wrap-git projects and tracked pc-bios
# binaries. ROM-source gitlinks are not built by this recipe.
SUBPROJECTS = ("dtc", "keycodemapdb", "berkeley-softfloat-3", "berkeley-testfloat-3")
REQUIRED_FIRMWARE = ("bios-256k.bin", "vgabios-stdvga.bin", "kvmvapic.bin", "efi-e1000.rom")


class InputError(ValueError):
    pass


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def input_key(inputs):
    return hashlib.sha256(canonical_bytes(inputs)).hexdigest()


def require_fields(value, fields, label):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise InputError("{} must contain exactly: {}".format(label, ", ".join(fields)))


def require_string(value, label):
    if not isinstance(value, str) or not value.strip() or "\0" in value:
        raise InputError("{} must be a nonempty string without NUL bytes".format(label))


def require_revision(value, label):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40}", value):
        raise InputError("{} must be a full lowercase Git SHA-1".format(label))


def validate_request(request):
    require_fields(request, ("sources", "profile", "container_image", "target",
                             "configure_args", "build_environment", "dependencies"), "request")
    require_fields(request["sources"], ("qemu", "simulator"), "sources")
    for name, source in request["sources"].items():
        require_fields(source, ("revision", "target_branch"), name)
        require_revision(source["revision"], name + ".revision")
        require_string(source["target_branch"], name + ".target_branch")
    profile = request["profile"]
    if (not isinstance(profile, dict) or type(profile.get("id")) is not int
            or profile["id"] <= 0 or not isinstance(profile.get("qemu"), dict)):
        raise InputError("profile must be the effective configuration object with a positive id")
    sections = {
        "kernel": ("branch", "compileContainer"),
        "qemu": ("branch", "compileContainer"),
        "nvme-cli": ("branch", "compileContainer"),
        "hostTests": ("compileContainer", "runContainer"),
        "guestTests": ("guestVmImage", "guestVmBuildContainer", "compileContainer"),
    }
    for section, fields in sections.items():
        if not isinstance(profile.get(section), dict):
            raise InputError("effective profile is missing section: " + section)
        for field in fields:
            require_string(profile[section].get(field), "profile." + section + "." + field)
    if profile["qemu"].get("branch") != request["sources"]["qemu"]["target_branch"]:
        raise InputError("effective profile and QEMU target branch disagree")
    require_string(request["container_image"], "container_image")
    require_string(request["target"], "target")
    if not isinstance(request["configure_args"], list):
        raise InputError("configure_args must be an ordered argument array")
    for arg in request["configure_args"]:
        require_string(arg, "configure argument")
        if arg == "--target-list" or arg.startswith("--target-list="):
            raise InputError("supply target separately, not through --target-list")
    if not isinstance(request["build_environment"], dict):
        raise InputError("build_environment must map variable names to explicit string values")
    for name, value in request["build_environment"].items():
        if (not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name)
                or not isinstance(value, str) or "\0" in value):
            raise InputError("invalid build environment entry: " + name)
    if not isinstance(request["dependencies"], list):
        raise InputError("dependencies must be an array (possibly empty)")
    seen = set()
    for dependency in request["dependencies"]:
        require_fields(dependency, ("source", "url", "patch_set", "revision",
                                    "applied_revision", "target_branch"), "dependency")
        source = dependency["source"]
        if not isinstance(source, str) or source not in request["sources"]:
            raise InputError("dependency source must be qemu or simulator")
        require_string(dependency["url"], "dependency.url")
        if type(dependency["patch_set"]) is not int or dependency["patch_set"] <= 0:
            raise InputError("dependency.patch_set must be a positive integer")
        for field in ("revision", "applied_revision"):
            require_revision(dependency[field], "dependency." + field)
        if dependency["target_branch"] != request["sources"][source]["target_branch"]:
            raise InputError("dependency target branch disagrees with its source")
        if dependency["url"] in seen:
            raise InputError("duplicate dependency URL: " + dependency["url"])
        seen.add(dependency["url"])
    canonical_bytes(request)  # Reject non-finite values anywhere in the profile.


def command_output(arguments):
    try:
        return subprocess.check_output(arguments, stderr=subprocess.PIPE)
    except subprocess.CalledProcessError as error:
        detail = error.stderr.decode("utf-8", errors="replace").strip()
        raise InputError("{} failed: {}".format(arguments[0], detail))
    except OSError as error:
        raise InputError("cannot execute {}: {}".format(arguments[0], error))


def git(repo, *arguments):
    return command_output(["git", "--no-optional-locks", "-C", repo] + list(arguments))


def source_identity(repo, requested):
    top = git(repo, "rev-parse", "--show-toplevel").decode().strip()
    if os.path.realpath(top) != os.path.realpath(repo):
        raise InputError("source path must be a repository root: " + repo)
    revision = git(repo, "rev-parse", "HEAD").decode().strip()
    if revision != requested["revision"]:
        raise InputError("source HEAD differs from requested revision: " + repo)
    dirty = git(repo, "status", "--porcelain=v1", "--untracked-files=no",
                "--ignore-submodules=untracked")
    if dirty:
        raise InputError("tracked source or submodule changes are not recorded: " + repo)
    entries = {}
    for entry in git(repo, "ls-tree", "-rz", "--full-tree", revision).split(b"\0"):
        if not entry:
            continue
        header, path = entry.split(b"\t", 1)
        mode, kind, object_id = header.decode("ascii").split()
        entries[os.fsdecode(path)] = (mode, kind, object_id)
    return {
        "revision": revision,
        "target_branch": requested["target_branch"],
        "tree": git(repo, "rev-parse", revision + "^{tree}").decode().strip(),
        "submodules": {path: entry[2] for path, entry in entries.items()
                       if entry[0] == "160000"},
    }, entries


def recipe_identity(simulator_dir, entries):
    recipes = {}
    for path in RECIPE_FILES:
        entry = entries.get(path)
        if entry is None or entry[0] not in ("100644", "100755"):
            raise InputError("required build recipe is not a tracked regular file: " + path)
        content = git(simulator_dir, "cat-file", "blob", entry[2])
        recipes[path] = {"mode": entry[0], "sha256": hashlib.sha256(content).hexdigest()}
    with open(__file__, "rb") as stream:
        running_helper = hashlib.sha256(stream.read()).hexdigest()
    if recipes[RECIPE_FILES[0]]["sha256"] != running_helper:
        raise InputError("identity helper differs from the recorded simulator recipe")
    return recipes


def inspect_image(reference):
    info = json.loads(command_output([
        "docker", "image", "inspect", "--format", "{{json .}}", "--", reference,
    ]).decode("utf-8"))
    image_id = info.get("Id") if isinstance(info, dict) else None
    if not isinstance(image_id, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", image_id):
        raise InputError("Docker did not return an immutable sha256 image ID")
    for field in ("Os", "Architecture"):
        require_string(info.get(field), "Docker " + field)
    variant = info.get("Variant", "")
    if not isinstance(variant, str):
        raise InputError("Docker Variant must be a string")
    return {"image_id": image_id, "os": info["Os"],
            "architecture": info["Architecture"], "variant": variant}


def identify(request, qemu_dir, simulator_dir):
    validate_request(request)
    repos = {"qemu": qemu_dir, "simulator": simulator_dir}
    qemu, _ = source_identity(qemu_dir, request["sources"]["qemu"])
    simulator, simulator_entries = source_identity(simulator_dir, request["sources"]["simulator"])
    sources = {"qemu": qemu, "simulator": simulator}
    recipes = recipe_identity(simulator_dir, simulator_entries)
    dependencies = sorted(request["dependencies"], key=lambda dep: dep["url"])
    for dependency in dependencies:
        repo = repos[dependency["source"]]
        # A declared review SHA may differ after a cherry-pick. Check the
        # recorded applied SHA against the actual source that will be built.
        try:
            git(repo, "merge-base", "--is-ancestor", dependency["applied_revision"],
                sources[dependency["source"]]["revision"])
        except InputError as error:
            raise InputError("dependency {} applied commit {} is not reachable from {} HEAD: {}".format(
                dependency["url"], dependency["applied_revision"], dependency["source"], error))
    inputs = {
        "schema_version": SCHEMA_VERSION,
        "sources": sources,
        "profile": request["profile"],
        "container": inspect_image(request["container_image"]),
        "target": request["target"],
        "configure_args": request["configure_args"],
        "build_environment": request["build_environment"],
        "recipes": recipes,
        "dependencies": dependencies,
    }
    key = input_key(inputs)
    return {
        "key": key,
        "inputs": inputs,
        "container_reference": request["container_image"],
        "bundle_relative_path": "qemu/" + key,
        "manifest_relative_path": "qemu/" + key + "/manifest.json",
    }


def export_tree(repo, revision, destination):
    """Export committed files only, without copying working-tree outputs."""
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryFile() as archive:
        process = subprocess.run(["git", "-C", str(repo), "archive", "--format=tar", revision],
                                 stdout=archive, stderr=subprocess.PIPE)
        if process.returncode:
            raise InputError("cannot export {} at {}: {}".format(
                repo, revision, process.stderr.decode(errors="replace")))
        archive.seek(0)
        process = subprocess.run(["tar", "-xf", "-", "--no-same-owner", "-C", str(destination)],
                                 stdin=archive, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if process.returncode:
            raise InputError("cannot extract {}: {}".format(
                repo, process.stderr.decode(errors="replace")))


def prepare_subprojects(qemu, cache):
    """Use Git objects pinned by the recorded wraps, never cached outputs."""
    pins = {}
    for name in SUBPROJECTS:
        wrap = configparser.ConfigParser(interpolation=None)
        wrap.read(str(qemu / "subprojects" / (name + ".wrap")))
        if not wrap.has_section("wrap-git"):
            raise InputError("missing wrap-git definition: " + name)
        revision = wrap.get("wrap-git", "revision", fallback="")
        require_revision(revision, name + " wrap revision")
        repo = Path(cache) / name
        # A missing module directory must not resolve to its parent repository.
        if not (repo / ".git").exists():
            raise InputError("missing subproject Git repository: " + str(repo))
        destination = qemu / "subprojects" / name
        export_tree(repo, revision, destination)
        patch = wrap.get("wrap-git", "patch_directory", fallback=None)
        if patch:
            if patch != name:
                raise InputError("unsupported wrap patch directory: " + patch)
            shutil.copytree(qemu / "subprojects/packagefiles" / patch, destination, dirs_exist_ok=True)
        pins[name] = revision
    return pins


def run_logged(arguments, log_path, commands, input_data=None, timeout=None):
    """Keep subprocess output out of the JSON handoff and retain failures."""
    entry = {"argv": [str(arg) for arg in arguments], "log": str(log_path)}
    commands.append(entry)
    print("INFO: " + log_path.name, file=sys.stderr, flush=True)
    with log_path.open("wb") as stream:
        try:
            process = subprocess.run(entry["argv"], stdout=stream, stderr=subprocess.STDOUT,
                                     input=input_data, timeout=timeout)
        except subprocess.TimeoutExpired:
            entry["exit_status"] = "timeout"
            raise InputError("{} timed out; see {}".format(log_path.stem, log_path))
    entry["exit_status"] = process.returncode
    if process.returncode:
        raise InputError("{} failed with exit {}; see {}".format(
            log_path.stem, process.returncode, log_path))


def container_command(inputs, host_dir, mountpoint, workdir, readonly=False):
    volume = str(host_dir) + ":" + mountpoint + (":ro" if readonly else "")
    arguments = ["docker", "run", "--rm", "--user", "{}:{}".format(os.getuid(), os.getgid()),
                 "--entrypoint", "/usr/bin/env", "--volume", volume, "--workdir", workdir]
    for name, value in sorted(inputs["build_environment"].items()):
        arguments.extend(["--env", name + "=" + value])
    return arguments + [inputs["container"]["image_id"]]


def build(identity, qemu_dir, simulator_dir, destination, jobs, subproject_cache=None):
    """Retain a successful disposable build for publication in the next step."""
    inputs = identity["inputs"]
    flags = inputs["configure_args"]
    if (inputs["sources"]["qemu"]["target_branch"] != "11.0"
            or inputs["profile"]["qemu"]["compileContainer"] != "ubuntu-26.04"
            or inputs["target"] != "x86_64-softmmu"):
        raise InputError("isolated build currently supports QEMU 11/x86 on Ubuntu 26.04 only")
    if ("--enable-werror" not in flags or "--disable-download" not in flags
            or any(arg.startswith(("--disable-werror", "--enable-download")) for arg in flags)):
        raise InputError("isolated build requires --enable-werror and --disable-download")
    if type(jobs) is not int or jobs <= 0:
        raise InputError("jobs must be a positive integer")
    destination = Path(destination).absolute()
    if ":" in str(destination):
        raise InputError("build directory cannot contain ':' (Docker volume separator)")
    # exist_ok=False also rejects links and previous successful/failed jobs.
    destination.mkdir()
    logs = destination / "logs"
    logs.mkdir()
    workspace = destination / "workspace"
    commands = []
    success = False
    try:
        (destination / "inputs.json").write_bytes(canonical_bytes(identity) + b"\n")
        qemu = workspace / "qemu"
        simulator = workspace / "simulator"
        export_tree(qemu_dir, inputs["sources"]["qemu"]["revision"], qemu)
        # Export only simulator inputs and recipes: no open-osd submodule or
        # legacy tool outputs are needed by the QEMU 11 compilation.
        export_tree(simulator_dir, inputs["sources"]["simulator"]["revision"], simulator)
        pins = prepare_subprojects(qemu, subproject_cache or Path(qemu_dir) / "subprojects")
        container = container_command(inputs, workspace, "/code", "/code/qemu")
        # No shell evaluation: arguments and environment values stay literal.
        run_logged(container + ["bash", "/code/simulator/infra/builder/prepare-qemu-vssim.sh",
                                "/code/qemu", "/code/simulator/eVSSIM"], logs / "prepare.log", commands)
        run_logged(container + ["./configure", "--target-list=" + inputs["target"]] + flags,
                   logs / "configure.log", commands)
        run_logged(container + ["ninja", "-C", "build", "-j" + str(jobs)], logs / "build.log", commands)
        run_logged(container + ["./build/qemu-system-x86_64", "--version"], logs / "version.log", commands)
        result = {"status": "built", "identity": identity, "qemu_dir": str(qemu),
                  "subprojects": pins, "commands": commands}
        (logs / "commands.json").write_bytes(canonical_bytes(commands) + b"\n")
        (destination / "build-result.json").write_bytes(canonical_bytes(result) + b"\n")
        success = True
        return result
    finally:
        # Logs survive errors; incomplete sources/binaries never become a
        # successful handoff. Publication and reusable manifests belong to step 4.
        try:
            if not success:
                (logs / "commands.json").write_bytes(canonical_bytes(commands) + b"\n")
        finally:
            if not success and workspace.exists():
                shutil.rmtree(workspace)


def file_inventory(folder):
    """Hash every payload file; bundles may not contain source-tree links."""
    files = {}
    for path in sorted(Path(folder).rglob("*")):
        if path.is_symlink():
            raise InputError("bundle contains a symlink: " + str(path))
        if path.is_dir():
            continue
        if not path.is_file():
            raise InputError("bundle contains a non-regular file: " + str(path))
        relative = path.relative_to(folder).as_posix()
        if relative == "manifest.json":
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        info = path.stat()
        files[relative] = {"sha256": digest.hexdigest(), "size": info.st_size,
                           "mode": info.st_mode & 0o777}
    return files


def bundle_path(folder, relative):
    if (not isinstance(relative, str) or not relative or Path(relative).is_absolute()
            or ".." in Path(relative).parts):
        raise InputError("invalid bundle-relative path: " + str(relative))
    return Path(folder) / relative


def verify_bundle(folder, identity):
    """Validate the manifest and all payload files before exposing a bundle."""
    folder = Path(folder)
    if folder.is_symlink() or not folder.is_dir():
        raise InputError("bundle must be a regular directory: " + str(folder))
    manifest_path = folder / "manifest.json"
    if manifest_path.is_symlink():
        raise InputError("manifest must be a regular file")
    with manifest_path.open(encoding="utf-8") as stream:
        manifest = json.load(stream, object_pairs_hook=unique_object)
    require_fields(manifest, ("schema_version", "key", "inputs", "version", "executable",
                              "data_dir", "files", "build"), "bundle manifest")
    if (manifest["schema_version"] != SCHEMA_VERSION or manifest["inputs"] != identity["inputs"]
            or manifest["key"] != identity["key"] or input_key(manifest["inputs"]) != identity["key"]
            or manifest["build"] != "full-clean-passed"):
        raise InputError("bundle manifest does not match the selected clean-build inputs")
    if not isinstance(manifest["version"], str) or not re.fullmatch(r"11\.0\.\d+", manifest["version"]):
        raise InputError("bundle version must identify QEMU 11.0")
    if file_inventory(folder) != manifest["files"]:
        raise InputError("bundle file inventory/checksums do not match")
    binary = bundle_path(folder, manifest["executable"])
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise InputError("bundle executable is missing or not executable")
    data = bundle_path(folder, manifest["data_dir"])
    for name in REQUIRED_FIRMWARE:
        if not (data / name).is_file() or (data / name).stat().st_size == 0:
            raise InputError("missing required firmware: " + name)
    return manifest


def probe_bundle(folder, identity, manifest, logs, commands, label):
    container = container_command(identity["inputs"], folder, "/artifact", "/tmp", readonly=True)
    name = "evssim-bundle-" + uuid.uuid4().hex
    container[2:2] = ["--interactive", "--network", "none", "--name", name]
    binary = "/artifact/" + manifest["executable"]
    version_log = logs / (label + "-version.log")
    try:
        run_logged(container + [binary, "--version"], version_log, commands, timeout=30)
        lines = version_log.read_text().splitlines()
        if not lines or lines[0] != "QEMU emulator version " + manifest["version"]:
            raise InputError("bundle executable version does not match its manifest")
        # A paused PC still loads BIOS/VGA ROMs during initialization. This
        # checks packaged firmware, without a guest image or a migration test.
        run_logged(container + [binary, "-L", "/artifact/" + manifest["data_dir"],
                                "-machine", "pc,accel=tcg", "-m", "64", "-display", "none",
                                "-nic", "none", "-S", "-monitor", "stdio"],
                   logs / (label + "-firmware.log"), commands, input_data=b"quit\n", timeout=30)
    except (InputError, OSError):
        # Killing a timed-out Docker client alone can leave its container.
        try:
            command_output(["docker", "rm", "--force", name])
        except InputError:
            pass  # --rm may already have removed it after an ordinary failure.
        raise


def atomic_bundle(parent, identity, populate, logs, commands, label):
    """Prepare on the destination filesystem and serialize final selection."""
    if parent.is_symlink():
        raise InputError("bundle parent cannot be a symlink: " + str(parent))
    parent.mkdir(parents=True, exist_ok=True)
    key = identity["key"]
    final = parent / key
    with tempfile.TemporaryDirectory(prefix="." + key + "-", dir=str(parent)) as temporary:
        candidate = Path(temporary) / "bundle"
        populate(candidate)
        manifest = verify_bundle(candidate, identity)
        probe_bundle(candidate, identity, manifest, logs, commands, label)
        # Do not overwrite either a concurrent winner or an invalid entry.
        with (parent / ("." + key + ".lock")).open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if final.exists() or final.is_symlink():
                manifest = verify_bundle(final, identity)
                probe_bundle(final, identity, manifest, logs, commands, label + "-existing")
            else:
                candidate.rename(final)
    return final


def publish(result, artifact_root, dist_root):
    """Install without compiling, publish a complete bundle, then stage it."""
    identity = result["identity"]
    if result["status"] != "built" or any(c.get("exit_status") != 0 for c in result["commands"]):
        raise InputError("publication requires a successful full clean build")
    artifact_root, dist_root = Path(artifact_root).resolve(), Path(dist_root).resolve()
    if artifact_root == dist_root:
        raise InputError("persistent artifacts and dist must have separate roots")
    if ":" in str(artifact_root) or ":" in str(dist_root):
        raise InputError("bundle roots cannot contain ':' (Docker volume separator)")
    qemu = Path(result["qemu_dir"])
    job = qemu.parent.parent
    logs = job / "logs"
    commands = []
    install = qemu.parent / "install"
    if install.exists():
        raise InputError("install staging directory already exists")
    try:
        container = container_command(identity["inputs"], qemu.parent, "/code", "/code/qemu")
        run_logged(container + ["./build/pyvenv/bin/meson", "install", "-C", "build", "--no-rebuild",
                                "--destdir", "/code/install"], logs / "install.log", commands)
        options = {item["name"]: item["value"] for item in json.loads(
            (qemu / "build/meson-info/intro-buildoptions.json").read_text())}
        prefix = options["prefix"]
        if not isinstance(prefix, str) or not prefix.startswith("/") or ".." in Path(prefix).parts:
            raise InputError("installation prefix must be an absolute path")
        payload = install / prefix.lstrip("/")
        executable = (Path(options["bindir"]) / "qemu-system-x86_64").as_posix()
        data_dir = (Path(options["datadir"]) / "qemu").as_posix()
        bundle_path(payload, executable)
        bundle_path(payload, data_dir)
        expected_version = (qemu / "VERSION").read_text().strip()
        # Install destinations outside the prefix cannot silently disappear.
        installed = json.loads((qemu / "build/meson-info/intro-installed.json").read_text())
        for target in installed.values():
            if not isinstance(target, str) or not target.startswith(prefix.rstrip("/") + "/"):
                raise InputError("installation target is outside the bundle prefix: " + str(target))
            source = install / target.lstrip("/")
            if not source.is_file() or source.is_symlink():
                raise InputError("installed payload must contain regular files: " + str(source))

        def populate(candidate):
            # Reject links rather than copying dependencies from outside the
            # isolated install. The generated bundle itself has no links.
            file_inventory(payload)
            shutil.copytree(payload, candidate)
            manifest = {"schema_version": SCHEMA_VERSION, "key": identity["key"],
                        "inputs": identity["inputs"], "version": expected_version,
                        "executable": executable, "data_dir": data_dir,
                        "files": file_inventory(candidate), "build": "full-clean-passed"}
            (candidate / "manifest.json").write_bytes(canonical_bytes(manifest) + b"\n")

        persistent = atomic_bundle(artifact_root / "qemu", identity, populate, logs, commands, "publish")
        staged = atomic_bundle(dist_root / "qemu", identity,
                               lambda candidate: shutil.copytree(persistent, candidate), logs, commands, "stage")
        return {"status": "published", "key": identity["key"], "bundle": str(persistent),
                "staged_bundle": str(staged), "manifest": str(staged / "manifest.json")}
    finally:
        try:
            (logs / "publication-commands.json").write_bytes(canonical_bytes(commands) + b"\n")
        finally:
            if install.exists():
                shutil.rmtree(install)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise InputError("duplicate JSON field: " + key)
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--request", required=True, help="resolved build-input JSON")
    parser.add_argument("--qemu-dir", required=True, help="exact QEMU source checkout")
    parser.add_argument("--simulator-dir", required=True, help="authoritative simulator checkout")
    parser.add_argument("--build-dir", help="new disposable build directory; omit for identity only")
    parser.add_argument("--jobs", type=int, default=4, help="parallel build jobs (default: 4)")
    parser.add_argument("--subproject-cache", help="source Git repositories for pinned Meson projects")
    parser.add_argument("--artifact-root", help="persistent artifact root (normally ROOT/artifacts)")
    parser.add_argument("--dist-root", help="staging root selected by EVSSIM_DIST_FOLDER")
    args = parser.parse_args()
    try:
        if bool(args.artifact_root) != bool(args.dist_root) or (args.artifact_root and not args.build_dir):
            raise InputError("publication requires --build-dir, --artifact-root, and --dist-root together")
        with open(args.request, encoding="utf-8") as stream:
            request = json.load(stream, object_pairs_hook=unique_object)
        result = identify(request, args.qemu_dir, args.simulator_dir)
        if args.build_dir:
            result = build(result, args.qemu_dir, args.simulator_dir, args.build_dir,
                           args.jobs, args.subproject_cache)
        elif args.subproject_cache:
            raise InputError("--subproject-cache requires --build-dir")
        if args.artifact_root:
            result = publish(result, args.artifact_root, args.dist_root)
        # Nothing is emitted until all requested checks/build stages succeed.
        print(canonical_bytes(result).decode("ascii"))
        return 0
    except (InputError, OSError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
