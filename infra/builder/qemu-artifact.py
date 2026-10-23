#!/usr/bin/env python3
"""Describe exact QEMU build inputs; do not build or select a cached artifact."""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys


SCHEMA_VERSION = 1
RECIPE_FILES = (
    "infra/builder/qemu-artifact.py",
    "infra/builder/compile-qemu.sh",
    "infra/builder/prepare-qemu-vssim.sh",
    "infra/builder/builder.sh",
)


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
    args = parser.parse_args()
    try:
        with open(args.request, encoding="utf-8") as stream:
            request = json.load(stream, object_pairs_hook=unique_object)
        result = identify(request, args.qemu_dir, args.simulator_dir)
        # Nothing is emitted until every input has been checked. This is an
        # input identity, not proof that the bundle exists or passed a build.
        print(canonical_bytes(result).decode("ascii"))
        return 0
    except (InputError, OSError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
