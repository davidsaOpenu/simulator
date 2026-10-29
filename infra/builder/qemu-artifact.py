#!/usr/bin/env python3
"""Identify QEMU inputs and optionally perform an isolated full clean build."""

import argparse
import configparser
import copy
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
LEGACY_SUBMODULES = ("dtc", "capstone", "ui/keycodemapdb")
REQUIRED_FIRMWARE = ("bios-256k.bin", "vgabios-stdvga.bin", "kvmvapic.bin", "efi-e1000.rom")
VSSIM_WWW_DIR = "vssim/simulator/www"
REQUIRED_MONITOR_ASSETS = ("index.html", "index.css", "favicon.ico")


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


def resolve_request(report, profiles, policy, qemu_dir, simulator_dir,
                    explicit_profile=None, simulator_branch="master"):
    """Resolve an application record into the existing artifact request shape."""
    require_fields(report, ("schema_version", "status", "dependencies"), "dependency report")
    if (type(report["schema_version"]) is not int or report["schema_version"] != 1
            or report["status"] != "complete"
            or not isinstance(report["dependencies"], list)):
        raise InputError("dependency report is incomplete or failed; no artifact can be selected")
    require_fields(profiles, ("configs",), "profiles")
    if not isinstance(profiles["configs"], list):
        raise InputError("configs must be an array")
    configs = {}
    for profile in profiles["configs"]:
        if (not isinstance(profile, dict) or type(profile.get("id")) is not int
                or profile["id"] <= 0 or profile["id"] in configs):
            raise InputError("profile IDs must be unique positive integers")
        configs[profile["id"]] = profile
    require_fields(policy, ("default_profile", "qemu_branches", "builds"), "selection policy")
    if (type(policy["default_profile"]) is not int or policy["default_profile"] <= 0
            or not isinstance(policy["qemu_branches"], dict)
            or not isinstance(policy["builds"], dict)):
        raise InputError("selection policy needs a default profile, branch map, and build recipes")
    for branch, profile_id in policy["qemu_branches"].items():
        require_string(branch, "policy branch")
        if type(profile_id) is not int or profile_id <= 0:
            raise InputError("policy branch mappings must name positive profile IDs")

    branches = {"qemu": set(), "simulator": set()}
    dependencies = []
    urls = set()
    for dependency in report["dependencies"]:
        require_fields(dependency, ("project", "url", "patch_set", "revision", "ref",
                                    "target_branch", "applied_revision", "disposition", "error"),
                       "dependency disposition")
        if (dependency["project"] not in ("kernel", "dnvme", "nvme-cli", "qemu", "simulator",
                                          "tnvme", "open-osd")
                or dependency["disposition"] not in ("applied", "already_present")
                or dependency["error"] != ""):
            raise InputError("dependency is not successfully applied: " + str(dependency["url"]))
        for field in ("url", "target_branch", "ref"):
            require_string(dependency[field], "dependency." + field)
        require_revision(dependency["revision"], "dependency.revision")
        require_revision(dependency["applied_revision"], "dependency.applied_revision")
        ref = re.fullmatch(r"refs/changes/[0-9]{2}/[0-9]+/([1-9][0-9]*)", dependency["ref"])
        if (type(dependency["patch_set"]) is not int or not ref
                or dependency["patch_set"] != int(ref.group(1))):
            raise InputError("dependency patch set must match its recorded Gerrit ref")
        if dependency["url"] in urls:
            raise InputError("duplicate dependency URL: " + dependency["url"])
        urls.add(dependency["url"])
        project = dependency["project"]
        if project in branches:
            branches[project].add(dependency["target_branch"])
            dependencies.append({"source": project, **{name: dependency[name] for name in
                                 ("url", "patch_set", "revision", "applied_revision", "target_branch")}})
    if any(len(values) > 1 for values in branches.values()):
        raise InputError("dependencies disagree on a source target branch")
    qemu_branch = next(iter(branches["qemu"]), None)
    if qemu_branch is not None and qemu_branch not in policy["qemu_branches"]:
        raise InputError("no profile policy for QEMU branch: " + qemu_branch)
    if explicit_profile is not None:
        if type(explicit_profile) is not int or explicit_profile <= 0:
            raise InputError("explicit profile must be a positive integer")
        selected_id = explicit_profile
    elif qemu_branch is not None:
        selected_id = policy["qemu_branches"][qemu_branch]
    else:
        selected_id = policy["default_profile"]
    if selected_id not in configs:
        raise InputError("selected profile does not exist: " + str(selected_id))
    selected = copy.deepcopy(configs[selected_id])
    if not isinstance(selected.get("qemu"), dict):
        raise InputError("selected profile is missing its QEMU settings")
    selected_branch = selected["qemu"].get("branch")
    require_string(selected_branch, "selected QEMU branch")
    if selected_branch not in policy["qemu_branches"]:
        raise InputError("selected profile has no supported QEMU branch policy")
    if qemu_branch is not None and selected_branch != qemu_branch:
        raise InputError("selected profile conflicts with the required QEMU target branch")
    container = selected["qemu"].get("compileContainer")
    require_string(container, "selected QEMU container")
    if container not in policy["builds"]:
        raise InputError("no build recipe for the selected QEMU container: " + str(container))
    recipe = policy["builds"][container]
    require_fields(recipe, ("container_image", "target", "configure_args", "build_environment"),
                   "container build recipe")
    simulator_branch = next(iter(branches["simulator"]), simulator_branch)
    request = {"sources": {
        "qemu": {"revision": git(qemu_dir, "rev-parse", "HEAD").decode().strip(),
                 "target_branch": selected_branch},
        "simulator": {"revision": git(simulator_dir, "rev-parse", "HEAD").decode().strip(),
                      "target_branch": simulator_branch},
    }, "profile": selected, "dependencies": dependencies, **copy.deepcopy(recipe)}
    validate_request(request)
    return request


def request_from_inputs(record):
    """Validate saved input data and recover its immutable-image request."""
    if not isinstance(record, dict) or not isinstance(record.get("inputs"), dict):
        raise InputError("resolved identity/manifest must contain its inputs")
    inputs = record["inputs"]
    require_fields(inputs, ("schema_version", "sources", "profile", "container", "target",
                            "configure_args", "build_environment", "recipes", "dependencies"),
                   "resolved inputs")
    if (type(inputs["schema_version"]) is not int or inputs["schema_version"] != SCHEMA_VERSION
            or input_key(inputs) != record.get("key")):
        raise InputError("resolved input key/schema does not match its contents")
    require_fields(inputs["sources"], ("qemu", "simulator"), "resolved sources")
    sources = {}
    for name, source in inputs["sources"].items():
        if not isinstance(source, dict) or not {"revision", "target_branch"} <= source.keys():
            raise InputError("resolved source is missing revision/branch: " + name)
        sources[name] = {field: source[field] for field in ("revision", "target_branch")}
    container = inputs["container"]
    if (not isinstance(container, dict) or not isinstance(container.get("image_id"), str)
            or not re.fullmatch(r"sha256:[0-9a-f]{64}", container["image_id"])):
        raise InputError("resolved container must contain its immutable image ID")
    request = {name: inputs[name] for name in
               ("profile", "target", "configure_args", "build_environment", "dependencies")}
    request.update(sources=sources, container_image=container["image_id"])
    validate_request(request)
    return copy.deepcopy(request)


def profile_from_inputs(record):
    """Load a frozen effective profile without Docker or source-tree actions."""
    return request_from_inputs(record)["profile"]


def validate_profile_environment(profile, environment):
    """Require the loaded component settings to match the selected profile."""
    expected = {"EVSSIM_VERSIONS_CONFIGURATION_ID": str(profile["id"])}
    sections = {
        "kernel": ("KERNEL", ("branch", "compileContainer")),
        "qemu": ("QEMU", ("branch", "compileContainer")),
        "nvme-cli": ("NVME_CLI", ("branch", "compileContainer")),
        "hostTests": ("HOST_TESTS", ("compileContainer", "runContainer")),
        "guestTests": ("GUEST_TESTS", ("guestVmImage", "guestVmBuildContainer", "compileContainer")),
    }
    for section, (prefix, fields) in sections.items():
        for field in fields:
            suffix = re.sub(r"([A-Z])", r"_\1", field).upper()
            expected["EVSSIM_" + prefix + "_" + suffix] = profile[section][field]
    for name, value in expected.items():
        if environment.get(name) != value:
            raise InputError("environment conflicts with the resolved input record: " + name)


def validate_saved_inputs(record, qemu_dir, simulator_dir, environment=None):
    """Check a saved identity against current inputs without mutating them."""
    request = request_from_inputs(record)
    if environment is not None:
        validate_profile_environment(request["profile"], environment)
    current = identify(request, qemu_dir, simulator_dir)
    for field, value in current["inputs"].items():
        if value != record["inputs"][field]:
            raise InputError("recorded {} differ from the current inputs".format(field))
    return current


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
    qemu, qemu_entries = source_identity(qemu_dir, request["sources"]["qemu"])
    version = qemu_entries.get("VERSION")
    if version is None or version[0] not in ("100644", "100755"):
        raise InputError("QEMU VERSION must be a tracked regular file")
    qemu["version"] = git(qemu_dir, "cat-file", "blob", version[2]).decode("utf-8").strip()
    require_string(qemu["version"], "QEMU VERSION")
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


def prepare_legacy_submodules(identity, source, destination):
    """Export only the recorded local Git objects needed by legacy x86."""
    recorded = identity["inputs"]["sources"]["qemu"]["submodules"]
    pins = {}
    for path in LEGACY_SUBMODULES:
        pin = recorded.get(path)
        require_revision(pin, "legacy submodule " + path)
        repo = Path(source) / path
        if not (repo / ".git").exists():
            raise InputError("missing legacy submodule repository: " + str(repo))
        top = git(str(repo), "rev-parse", "--show-toplevel").decode().strip()
        if Path(top).resolve() != repo.resolve():
            raise InputError("legacy submodule path is not a repository root: " + str(repo))
        export_tree(str(repo), pin, Path(destination) / path)
        pins[path] = pin
    return pins


def prepare_legacy_configure(qemu):
    """Use prepared regular VSSIM files in the disposable legacy export."""
    qemu = Path(qemu)
    staging = qemu / "hw/vssim/simulator"
    file_inventory(staging)
    makefile = (qemu / "hw/vssim/Makefile.objs").read_text()
    makefile = "\n".join(line.split("#", 1)[0] for line in makefile.splitlines())
    objects = re.findall(r"simulator/([A-Za-z0-9_./-]+)\.o", makefile)
    if not objects:
        raise InputError("legacy VSSIM Makefile has no recognized simulator objects")
    missing = []
    for name in objects:
        if ".." in Path(name).parts or name.startswith("/"):
            raise InputError("invalid legacy simulator object path: " + name)
        if not (staging / (name + ".c")).is_file():
            missing.append(name + ".c")
    if missing:
        raise InputError("legacy VSSIM Makefile references missing simulator sources: " + ", ".join(sorted(set(missing))))

    configure = qemu / "configure"
    original = configure.read_text()
    ignored = []
    obsolete_targets = {"ssd_file_operations.c", "ssd_file_operations.h",
                        "ftl_perf_manager.c", "ftl_perf_manager.h", "ssd_monitor"}
    def prepared_link(match):
        relative = match.group(2)
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise InputError("invalid legacy VSSIM target: " + relative)
        target = staging / relative
        if not target.exists() and relative not in obsolete_targets:
            raise InputError("legacy configure requires an unprepared VSSIM target: " + relative)
        if not target.exists():
            ignored.append(relative)
        return ""
    prepared, count = re.subn(r'(?m)^[ \t]*symlink "\$vssim_source/([^"\n]+)" "\$vssim_target/([^"\n]+)"[ \t]*$',
                              prepared_link, original)
    prepared, schemas = re.subn(r'(?m)^[ \t]*make -C "\$vssim_target/osc-osd/osd-target" osd-schema\.c[ \t]*$',
                                "    # osd-schema.c was prepared as a regular file.", prepared)
    if not count or schemas != 1 or 'symlink "$vssim_source/' in prepared:
        raise InputError("unsupported legacy VSSIM configure preparation; refusing a partial adaptation")
    configure.write_text(prepared)
    return {"original_configure_sha256": hashlib.sha256(original.encode()).hexdigest(),
            "prepared_configure_sha256": hashlib.sha256(prepared.encode()).hexdigest(),
            "unused_absent_link_targets": sorted(ignored)}


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


def validate_build(identity, jobs):
    inputs = identity["inputs"]
    flags = inputs["configure_args"]
    source = inputs["sources"]["qemu"]
    profile = inputs["profile"]["qemu"]
    if (source["target_branch"] == "master" and profile["compileContainer"] == "ubuntu-14.04"
            and re.fullmatch(r"2\.12\.\d+", source["version"])):
        kind = "make"
    elif source["target_branch"] == "11.0" and profile["compileContainer"] == "ubuntu-26.04":
        kind = "ninja"
    else:
        raise InputError("isolated build currently supports QEMU 11/26.04 or legacy QEMU 2.12/14.04 only")
    if inputs["target"] != "x86_64-softmmu":
        raise InputError("isolated build currently supports x86_64-softmmu only")
    if kind == "ninja" and ("--enable-werror" not in flags or "--disable-download" not in flags
            or any(arg.startswith(("--disable-werror", "--enable-download"))
                   or (arg.startswith("-Dwerror=") and arg != "-Dwerror=true") for arg in flags)):
        raise InputError("isolated build requires --enable-werror and --disable-download")
    if kind == "make" and ("--enable-vssim" not in flags or "--disable-git-update" not in flags
            or any(arg.startswith(("--disable-vssim", "--enable-git-update")) for arg in flags)):
        raise InputError("legacy isolated build requires --enable-vssim and --disable-git-update")
    if type(jobs) is not int or jobs <= 0:
        raise InputError("jobs must be a positive integer")
    return kind


def verify_werror(qemu):
    options = json.loads((qemu / "build/meson-info/intro-buildoptions.json").read_text())
    if not any(option.get("name") == "werror" and option.get("value") is True
               for option in options):
        raise InputError("configured Meson werror must be true before compilation")


def build(identity, qemu_dir, simulator_dir, destination, jobs, subproject_cache=None):
    """Retain a successful disposable build for publication."""
    kind = validate_build(identity, jobs)
    if kind == "make" and subproject_cache is not None:
        raise InputError("legacy builds use recorded QEMU gitlinks; --subproject-cache is for Meson projects only")
    inputs = identity["inputs"]
    flags = inputs["configure_args"]
    destination = Path(destination).absolute()
    if ":" in str(destination):
        raise InputError("build directory cannot contain ':' (Docker volume separator)")
    # exist_ok=False also rejects links and previous successful/failed jobs.
    destination.mkdir(parents=True)
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
        if kind == "make":
            pins = prepare_legacy_submodules(identity, qemu_dir, qemu)
        else:
            pins = prepare_subprojects(qemu, subproject_cache or Path(qemu_dir) / "subprojects")
        container = container_command(inputs, workspace, "/code", "/code/qemu")
        # No shell evaluation: arguments and environment values stay literal.
        run_logged(container + ["bash", "/code/simulator/infra/builder/prepare-qemu-vssim.sh",
                                "/code/qemu", "/code/simulator/eVSSIM"] + (["--legacy"] if kind == "make" else []),
                   logs / "prepare.log", commands)
        legacy_preparation = prepare_legacy_configure(qemu) if kind == "make" else None
        run_logged(container + ["./configure", "--target-list=" + inputs["target"]] + flags,
                   logs / "configure.log", commands)
        if kind == "make":
            file_inventory(qemu / "hw/vssim/simulator")
            run_logged(container + ["make", "clean"], logs / "clean.log", commands)
            run_logged(container + ["make", "-j" + str(jobs)], logs / "build.log", commands)
            executable = "./x86_64-softmmu/qemu-system-x86_64"
        else:
            verify_werror(qemu)
            run_logged(container + ["ninja", "-C", "build", "-j" + str(jobs)], logs / "build.log", commands)
            executable = "./build/qemu-system-x86_64"
        run_logged(container + [executable, "--version"], logs / "version.log", commands)
        result = {"status": "built", "identity": identity, "qemu_dir": str(qemu),
                  "subprojects": pins, "commands": commands}
        if legacy_preparation is not None:
            result["legacy_preparation"] = legacy_preparation
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
                              "data_dir", "vssim_www_dir", "files", "build"), "bundle manifest")
    if (manifest["schema_version"] != SCHEMA_VERSION or manifest["inputs"] != identity["inputs"]
            or manifest["key"] != identity["key"] or input_key(manifest["inputs"]) != identity["key"]
            or manifest["build"] != "full-clean-passed"):
        raise InputError("bundle manifest does not match the selected clean-build inputs")
    version_pattern = r"2\.12\.\d+" if validate_build(identity, 1) == "make" else r"11\.0\.\d+"
    if (not isinstance(manifest["version"], str) or not re.fullmatch(version_pattern, manifest["version"])
            or manifest["version"] != identity["inputs"]["sources"]["qemu"]["version"]):
        raise InputError("bundle version must match the selected QEMU source VERSION")
    if file_inventory(folder) != manifest["files"]:
        raise InputError("bundle file inventory/checksums do not match")
    binary = bundle_path(folder, manifest["executable"])
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise InputError("bundle executable is missing or not executable")
    data = bundle_path(folder, manifest["data_dir"])
    for name in REQUIRED_FIRMWARE:
        if not (data / name).is_file() or (data / name).stat().st_size == 0:
            raise InputError("missing required firmware: " + name)
    if manifest["vssim_www_dir"] != VSSIM_WWW_DIR:
        raise InputError("bundle monitor assets must preserve the VSSIM working-directory layout")
    verify_monitor_assets(folder / VSSIM_WWW_DIR)
    return manifest


def verify_monitor_assets(folder):
    if folder.is_symlink():
        raise InputError("monitor asset directory must not be a symlink")
    for name in REQUIRED_MONITOR_ASSETS:
        asset = folder / name
        if asset.is_symlink() or not asset.is_file() or asset.stat().st_size == 0:
            raise InputError("missing, empty, or linked monitor asset: " + name)


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
        network = ["-net", "none"] if validate_build(identity, 1) == "make" else ["-nic", "none"]
        run_logged(container + [binary, "-L", "/artifact/" + manifest["data_dir"],
                                "-machine", "pc,accel=tcg", "-m", "64", "-display", "none",
                                *network, "-S", "-monitor", "stdio"],
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
    with (parent / ("." + key + ".lock")).open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if final.exists() or final.is_symlink():
            manifest = verify_bundle(final, identity)
            probe_bundle(final, identity, manifest, logs, commands, label + "-existing")
            return final
        # Leave copying/probing outside the lock, but avoid even allocating a
        # temporary directory when an already staged bundle can be reused.
        fcntl.flock(lock, fcntl.LOCK_UN)
        with tempfile.TemporaryDirectory(prefix="." + key + "-", dir=str(parent)) as temporary:
            candidate = Path(temporary) / "bundle"
            populate(candidate)
            manifest = verify_bundle(candidate, identity)
            probe_bundle(candidate, identity, manifest, logs, commands, label)
            # Do not overwrite either a concurrent winner or an invalid entry.
            fcntl.flock(lock, fcntl.LOCK_EX)
            if final.exists() or final.is_symlink():
                manifest = verify_bundle(final, identity)
                probe_bundle(final, identity, manifest, logs, commands, label + "-existing")
            else:
                candidate.rename(final)
    return final


def artifact_roots(artifact_root, dist_root):
    artifact_root, dist_root = Path(artifact_root).resolve(), Path(dist_root).resolve()
    if artifact_root == dist_root:
        raise InputError("persistent artifacts and dist must have separate roots")
    if ":" in str(artifact_root) or ":" in str(dist_root):
        raise InputError("bundle roots cannot contain ':' (Docker volume separator)")
    return artifact_root, dist_root


def legacy_payload(qemu, payload):
    """Copy the legacy install inputs without invoking its install: all rule."""
    configuration = {}
    fields = {"TOOLS", "EXESUF", "CONFIG_MODULES", "CONFIG_LINUX", "INSTALL_BLOBS"}
    for line in (qemu / "config-host.mak").read_text().splitlines():
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)=(.*)", line)
        if match and match[1] in fields:
            if match[1] in configuration:
                raise InputError("duplicate legacy configuration field: " + match[1])
            configuration[match[1]] = match[2].strip()
    if (configuration.get("CONFIG_LINUX") != "y" or configuration.get("EXESUF") != ""
            or configuration.get("CONFIG_MODULES", "") or configuration.get("INSTALL_BLOBS") != "yes"):
        raise InputError("legacy packaging requires Linux, firmware blobs, no executable suffix, and no shared modules")
    if "TOOLS" not in configuration:
        raise InputError("legacy configuration is missing TOOLS")

    def literal_paths(value):
        paths = value.split()
        for path in paths:
            if (not re.fullmatch(r"[A-Za-z0-9_,.+/-]+", path) or Path(path).is_absolute()
                    or any(part in (".", "..") for part in path.split("/"))):
                raise InputError("unsupported legacy install input: " + path)
        return paths

    makefile = re.sub(r"\\\n[ \t]*", " ", (qemu / "Makefile").read_text())
    def make_list(name):
        # BLOBS also has an empty assignment in the INSTALL_BLOBS else arm.
        values = [value.strip() for value in re.findall(r"(?m)^" + name + r"=(.*)$", makefile)
                  if value.strip()]
        if len(values) != 1:
            raise InputError("unsupported legacy Makefile " + name + " list")
        return literal_paths(values[0])

    tools = literal_paths(configuration["TOOLS"].replace("$(EXESUF)", ""))
    files = [("x86_64-softmmu/qemu-system-x86_64", "bin/qemu-system-x86_64"),
             ("qemu-bridge-helper", "libexec/qemu-bridge-helper"),
             ("trace-events-all", "share/qemu/trace-events-all")]
    files.extend((tool, "bin/" + Path(tool).name) for tool in tools)
    files.extend(("pc-bios/" + name, "share/qemu/" + name) for name in make_list("BLOBS"))
    files.extend(("pc-bios/keymaps/" + name, "share/qemu/keymaps/" + name)
                 for name in make_list("KEYMAPS"))
    destinations = set()
    for relative, destination in files:
        if destination in destinations:
            raise InputError("duplicate legacy payload path: " + destination)
        destinations.add(destination)
        source = qemu / relative
        if (any(path.is_symlink() for path in [source] + [qemu / part for part in Path(relative).parents])
                or not source.is_file() or source.stat().st_size == 0):
            raise InputError("legacy install input must be a nonempty regular file: " + relative)
        if destination.startswith(("bin/", "libexec/")) and not os.access(source, os.X_OK):
            raise InputError("legacy program is not executable: " + relative)
        target = payload / destination
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    return "bin/qemu-system-x86_64", "share/qemu"


def publish(result, artifact_root, dist_root):
    """Install without compiling, publish a complete bundle, then stage it."""
    identity = result["identity"]
    kind = validate_build(identity, 1)
    if result["status"] != "built" or any(c.get("exit_status") != 0 for c in result["commands"]):
        raise InputError("publication requires a successful full clean build")
    artifact_root, dist_root = artifact_roots(artifact_root, dist_root)
    qemu = Path(result["qemu_dir"])
    job = qemu.parent.parent
    logs = job / "logs"
    commands = []
    install = qemu.parent / "install"
    if install.exists():
        raise InputError("install staging directory already exists")
    try:
        if kind == "make":
            payload = install / "payload"
            executable, data_dir = legacy_payload(qemu, payload)
        else:
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
            # Install destinations outside the prefix cannot silently disappear.
            installed = json.loads((qemu / "build/meson-info/intro-installed.json").read_text())
            for target in installed.values():
                if not isinstance(target, str) or not target.startswith(prefix.rstrip("/") + "/"):
                    raise InputError("installation target is outside the bundle prefix: " + str(target))
                source = install / target.lstrip("/")
                if not source.is_file() or source.is_symlink():
                    raise InputError("installed payload must contain regular files: " + str(source))
        expected_version = (qemu / "VERSION").read_text().strip()

        def populate(candidate):
            # Reject links rather than copying dependencies from outside the
            # isolated install. The generated bundle itself has no links.
            file_inventory(payload)
            shutil.copytree(payload, candidate)
            monitor_source = qemu / "hw" / VSSIM_WWW_DIR
            verify_monitor_assets(monitor_source)
            monitor_target = candidate / VSSIM_WWW_DIR
            monitor_target.mkdir(parents=True)
            for name in REQUIRED_MONITOR_ASSETS:
                shutil.copyfile(monitor_source / name, monitor_target / name)
            manifest = {"schema_version": SCHEMA_VERSION, "key": identity["key"],
                        "inputs": identity["inputs"], "version": expected_version,
                        "executable": executable, "data_dir": data_dir, "vssim_www_dir": VSSIM_WWW_DIR,
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


def artifact_selection(identity, persistent, staged, cache):
    manifest = verify_bundle(staged, identity)
    return {"status": "selected", "cache": cache, "key": identity["key"],
            "bundle": str(persistent), "staged_bundle": str(staged),
            "manifest": str(staged / "manifest.json"),
            "executable": str(staged / manifest["executable"]),
            "vssim_www_dir": str(staged / manifest["vssim_www_dir"]),
            "data_dir": str(staged / manifest["data_dir"]), "version": manifest["version"],
            "binary_sha256": manifest["files"][manifest["executable"]]["sha256"],
            "container_image_id": identity["inputs"]["container"]["image_id"],
            "profile_id": identity["inputs"]["profile"]["id"],
            "sources": {name: source["revision"] for name, source in identity["inputs"]["sources"].items()}}


def verify_running_artifact(runtime, container):
    """Check the detached QEMU process against the frozen launch selection."""
    actual_image = command_output(["docker", "inspect", "--format", "{{.Image}}", container]).decode().strip()
    if actual_image != runtime["container_image_id"]:
        raise InputError("running container differs from the selected image")
    pid = command_output(["docker", "exec", container, "cat", "/tmp/qemu.pid"]).decode().strip()
    if not re.fullmatch(r"[1-9][0-9]*", pid):
        raise InputError("running QEMU PID is invalid")
    process = "/proc/" + pid
    executable = command_output(["docker", "exec", container, "readlink", process + "/exe"]).decode().strip()
    digest = command_output(["docker", "exec", container, "sha256sum", process + "/exe"]).decode().split()
    argv = command_output(["docker", "exec", container, "cat", process + "/cmdline"]).split(b"\0")
    data_args = [argv[i + 1] for i, arg in enumerate(argv[:-1]) if arg == b"-L"]
    if (executable != runtime["runtime_executable"] or not digest
            or digest[0] != runtime["binary_sha256"] or not argv
            or argv[0] != os.fsencode(runtime["runtime_executable"])
            or data_args != [os.fsencode(runtime["runtime_data_dir"])]):
        raise InputError("running QEMU executable/hash/arguments differ from the selected artifact")
    print("INFO: running artifact {} verified in container {} at PID {}".format(
        runtime["key"], container, pid), file=sys.stderr)


def runtime_selection(selection, profiles, environment, running_container=None):
    """Verify an explicit S2 selection before returning container launch paths."""
    require_fields(selection, ("status", "cache", "key", "bundle", "staged_bundle", "manifest",
                               "executable", "vssim_www_dir", "data_dir", "version", "binary_sha256",
                               "container_image_id", "profile_id", "sources"), "artifact selection")
    if selection["status"] != "selected" or selection["cache"] not in ("hit", "miss"):
        raise InputError("runtime requires a successfully selected artifact")
    for name in ("bundle", "staged_bundle"):
        require_string(selection[name], name)
        if not Path(selection[name]).is_absolute():
            raise InputError("selected bundle paths must be absolute")
    staged = Path(selection["staged_bundle"])
    manifest_path = staged / "manifest.json"
    if manifest_path.is_symlink():
        raise InputError("manifest must be a regular file")
    with manifest_path.open(encoding="utf-8") as stream:
        manifest = json.load(stream, object_pairs_hook=unique_object)
    request = request_from_inputs(manifest)
    require_fields(profiles, ("configs",), "profiles")
    matches = [p for p in profiles["configs"] if p.get("id") == request["profile"]["id"]]
    if len(matches) != 1 or matches[0] != request["profile"]:
        raise InputError("artifact profile differs from the declared configuration")
    validate_profile_environment(request["profile"], environment)
    identity = {"key": manifest["key"], "inputs": manifest["inputs"]}
    expected = artifact_selection(identity, Path(selection["bundle"]), staged, selection["cache"])
    if expected != selection:
        raise InputError("artifact selection differs from its verified bundle")
    if inspect_image(selection["container_image_id"]) != manifest["inputs"]["container"]:
        raise InputError("runtime container differs from the recorded image")
    executable = "/qemu-artifact/" + manifest["executable"]
    version = command_output(container_command(identity["inputs"], staged, "/qemu-artifact", "/tmp", readonly=True)
                             + [executable, "--version"]).decode().splitlines()
    if not version or version[0] != "QEMU emulator version " + manifest["version"]:
        raise InputError("runtime executable version differs from the manifest")
    runtime = {**selection, "runtime_executable": executable,
               "runtime_data_dir": "/qemu-artifact/" + manifest["data_dir"]}
    if running_container is not None:
        verify_running_artifact(runtime, running_container)
    return runtime


def select_artifact(identity, qemu_dir, simulator_dir, destination, jobs,
                    subproject_cache, artifact_root, dist_root):
    """Select only verified inputs/payloads; compile only when no entry exists."""
    kind = validate_build(identity, jobs)
    if kind == "make" and subproject_cache is not None:
        raise InputError("legacy builds use recorded QEMU gitlinks; --subproject-cache is for Meson projects only")
    artifact_root, dist_root = artifact_roots(artifact_root, dist_root)
    job = Path(destination).absolute()
    if job.exists() or job.is_symlink():
        raise InputError("selection requires a new job directory: " + str(job))
    parent = artifact_root / "qemu"
    if parent.is_symlink():
        raise InputError("bundle parent cannot be a symlink: " + str(parent))
    persistent = parent / identity["key"]
    if not persistent.exists() and not persistent.is_symlink():
        print("INFO: cache miss " + identity["key"], file=sys.stderr, flush=True)
        result = build(identity, qemu_dir, simulator_dir, str(job), jobs, subproject_cache)
        decision = {"cache": "miss", "key": identity["key"], "status": "failed"}
        try:
            published = publish(result, artifact_root, dist_root)
            selected = artifact_selection(identity, Path(published["bundle"]),
                                          Path(published["staged_bundle"]), "miss")
            decision["status"] = "selected"
            return selected
        finally:
            (job / "logs/cache.json").write_bytes(canonical_bytes(decision) + b"\n")

    print("INFO: checking cache entry " + identity["key"], file=sys.stderr, flush=True)
    job.mkdir(parents=True)
    logs = job / "logs"
    logs.mkdir()
    (job / "inputs.json").write_bytes(canonical_bytes(identity) + b"\n")
    commands = []
    decision = {"cache": "hit", "key": identity["key"], "status": "rejected"}
    try:
        with (parent / ("." + identity["key"] + ".lock")).open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            manifest = verify_bundle(persistent, identity)
            probe_bundle(persistent, identity, manifest, logs, commands, "cache")
            staged = atomic_bundle(dist_root / "qemu", identity,
                                   lambda candidate: shutil.copytree(persistent, candidate),
                                   logs, commands, "cache-stage")
            selected = artifact_selection(identity, persistent, staged, "hit")
        decision["status"] = "selected"
        return selected
    except (InputError, OSError, ValueError) as error:
        decision["error"] = str(error)
        raise
    finally:
        (logs / "cache-commands.json").write_bytes(canonical_bytes(commands) + b"\n")
        (logs / "cache.json").write_bytes(canonical_bytes(decision) + b"\n")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise InputError("duplicate JSON field: " + key)
        result[key] = value
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--request", help="resolved build-input JSON")
    mode.add_argument("--resolve-dependencies", help="JSON application record from the dependency handler")
    mode.add_argument("--profile-from-inputs", help="validate a saved identity/manifest and print its effective profile")
    mode.add_argument("--validate-inputs", help="check saved identity/manifest against current sources, recipes, and image")
    mode.add_argument("--saved-inputs", help="validate saved identity/manifest and use its exact build recipe")
    mode.add_argument("--runtime-selection", help="verify an S2 selection against the loaded profile and print launch paths")
    parser.add_argument("--check-environment", action="store_true", help="also require loaded profile fields to match saved inputs")
    parser.add_argument("--selection-policy", help="default/branch/container recipe policy JSON (default: versions/qemu-build-policy.json)")
    parser.add_argument("--profiles", help="profile JSON (default: versions/configs.json beside this helper)")
    parser.add_argument("--profile-id", type=int, help="explicit compatible configuration ID")
    parser.add_argument("--simulator-branch", help="simulator delivery branch when it has no dependency (default: master)")
    parser.add_argument("--qemu-dir", help="exact QEMU source checkout")
    parser.add_argument("--simulator-dir", help="authoritative simulator checkout")
    parser.add_argument("--build-dir", help="new disposable build directory; omit for identity only")
    parser.add_argument("--jobs", type=int, default=4, help="parallel build jobs (default: 4)")
    parser.add_argument("--subproject-cache", help="source Git repositories for pinned Meson projects")
    parser.add_argument("--artifact-root", help="persistent artifact root (normally ROOT/artifacts)")
    parser.add_argument("--dist-root", help="staging root selected by EVSSIM_DIST_FOLDER")
    parser.add_argument("--running-container", help="with --runtime-selection, also verify this detached QEMU process")
    args = parser.parse_args()
    try:
        if args.running_container and not args.runtime_selection:
            raise InputError("--running-container requires --runtime-selection")
        if bool(args.artifact_root) != bool(args.dist_root) or (args.artifact_root and not args.build_dir):
            raise InputError("publication requires --build-dir, --artifact-root, and --dist-root together")
        def read_json(path):
            with open(path, encoding="utf-8") as stream:
                return json.load(stream, object_pairs_hook=unique_object)
        if args.runtime_selection:
            if (args.qemu_dir or args.simulator_dir or args.build_dir or args.artifact_root
                    or args.dist_root or args.subproject_cache or args.selection_policy
                    or args.profile_id is not None or args.simulator_branch or args.check_environment):
                raise InputError("--runtime-selection verifies an existing selection; build/selection options are not allowed")
            result = runtime_selection(read_json(args.runtime_selection),
                                       read_json(args.profiles or Path(__file__).parent / "versions/configs.json"),
                                       os.environ, args.running_container)
            print(canonical_bytes(result).decode("ascii"))
            return 0
        if args.profile_from_inputs:
            if (args.qemu_dir or args.simulator_dir or args.build_dir or args.artifact_root
                    or args.dist_root or args.subproject_cache or args.selection_policy
                    or args.profiles or args.profile_id is not None or args.simulator_branch
                    or args.check_environment):
                raise InputError("--profile-from-inputs only loads a saved profile; build/selection options are not allowed")
            print(canonical_bytes(profile_from_inputs(read_json(args.profile_from_inputs))).decode("ascii"))
            return 0
        if not args.qemu_dir or not args.simulator_dir:
            raise InputError("artifact identification/build requires --qemu-dir and --simulator-dir")
        if args.validate_inputs:
            if (args.build_dir or args.artifact_root or args.dist_root or args.subproject_cache
                    or args.selection_policy or args.profiles or args.profile_id is not None
                    or args.simulator_branch):
                raise InputError("--validate-inputs only checks saved inputs; build/selection options are not allowed")
            result = validate_saved_inputs(read_json(args.validate_inputs), args.qemu_dir, args.simulator_dir,
                                           os.environ if args.check_environment else None)
            print(canonical_bytes(result).decode("ascii"))
            return 0
        if args.check_environment and not args.saved_inputs:
            raise InputError("--check-environment requires --validate-inputs or --saved-inputs")
        if args.saved_inputs:
            if (args.selection_policy or args.profiles or args.profile_id is not None or args.simulator_branch):
                raise InputError("--saved-inputs uses its frozen profile/recipe; selection overrides are not allowed")
            # Read exactly once. File changes during inspection/build cannot
            # replace the validated input snapshot used for execution.
            result = validate_saved_inputs(read_json(args.saved_inputs), args.qemu_dir, args.simulator_dir,
                                           os.environ if args.check_environment else None)
        elif args.resolve_dependencies:
            request = resolve_request(read_json(args.resolve_dependencies),
                                      read_json(args.profiles or Path(__file__).parent / "versions/configs.json"),
                                      read_json(args.selection_policy or Path(__file__).parent / "versions/qemu-build-policy.json"),
                                      args.qemu_dir, args.simulator_dir,
                                      args.profile_id, args.simulator_branch or "master")
            print("INFO: selected profile {} for QEMU {} at {}; simulator {} at {}".format(
                request["profile"]["id"], request["sources"]["qemu"]["target_branch"],
                request["sources"]["qemu"]["revision"], request["sources"]["simulator"]["target_branch"],
                request["sources"]["simulator"]["revision"]), file=sys.stderr)
            print("INFO: effective profile " + canonical_bytes(request["profile"]).decode("ascii"), file=sys.stderr)
        else:
            if (args.selection_policy or args.profiles or args.profile_id is not None
                    or args.simulator_branch):
                raise InputError("profile-selection options require --resolve-dependencies")
            request = read_json(args.request)
        if not args.saved_inputs:
            result = identify(request, args.qemu_dir, args.simulator_dir)
        if args.artifact_root:
            result = select_artifact(result, args.qemu_dir, args.simulator_dir, args.build_dir,
                                     args.jobs, args.subproject_cache, args.artifact_root, args.dist_root)
        elif args.build_dir:
            result = build(result, args.qemu_dir, args.simulator_dir, args.build_dir,
                           args.jobs, args.subproject_cache)
        elif args.subproject_cache:
            raise InputError("--subproject-cache requires --build-dir")
        # Nothing is emitted until all requested checks/build stages succeed.
        print(canonical_bytes(result).decode("ascii"))
        return 0
    except (InputError, OSError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
