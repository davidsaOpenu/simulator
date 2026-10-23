#!/usr/bin/env python3
"""Resolve a dependency report into QEMU source/profile inputs without building."""

import argparse
import copy
import json
from pathlib import Path
import re
import subprocess
import sys


class InputError(ValueError):
    pass


def require_fields(value, fields, label):
    if not isinstance(value, dict) or set(value) != set(fields):
        raise InputError("{} must contain exactly: {}".format(label, ", ".join(fields)))


def require_string(value, label):
    if not isinstance(value, str) or not value.strip() or "\0" in value:
        raise InputError(label + " must be a nonempty string without NUL bytes")


def require_revision(value, label):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{40}", value):
        raise InputError(label + " must be a full lowercase Git SHA-1")


def git_head(repo):
    try:
        return subprocess.check_output(
            ["git", "--no-optional-locks", "-C", str(repo), "rev-parse", "HEAD"],
            stderr=subprocess.PIPE, text=True).strip()
    except subprocess.CalledProcessError as error:
        raise InputError("cannot read source HEAD: " + error.stderr.strip()) from error


def validate_profile(profile):
    sections = {
        "kernel": ("branch", "compileContainer"),
        "qemu": ("branch", "compileContainer"),
        "nvme-cli": ("branch", "compileContainer"),
        "hostTests": ("compileContainer", "runContainer"),
        "guestTests": ("guestVmImage", "guestVmBuildContainer", "compileContainer"),
    }
    for section, fields in sections.items():
        if not isinstance(profile.get(section), dict):
            raise InputError("selected profile is missing section: " + section)
        for field in fields:
            require_string(profile[section].get(field), "profile." + section + "." + field)


def resolve_inputs(report, profiles, policy, qemu_dir, simulator_dir,
                   explicit_profile=None, simulator_branch="master"):
    require_fields(report, ("schema_version", "status", "dependencies"), "dependency report")
    if (type(report["schema_version"]) is not int or report["schema_version"] != 1
            or report["status"] != "complete"
            or not isinstance(report["dependencies"], list)):
        raise InputError("dependency report is incomplete or failed")
    require_fields(profiles, ("configs",), "profiles")
    if not isinstance(profiles["configs"], list):
        raise InputError("configs must be an array")
    configs = {}
    for profile in profiles["configs"]:
        if (not isinstance(profile, dict) or type(profile.get("id")) is not int
                or profile["id"] <= 0 or profile["id"] in configs):
            raise InputError("profile IDs must be unique positive integers")
        configs[profile["id"]] = profile
    # Selection policy contains only mappings. Build recipes belong to S2c.
    require_fields(policy, ("default_profile", "qemu_branches"), "selection policy")
    if (type(policy["default_profile"]) is not int or policy["default_profile"] <= 0
            or not isinstance(policy["qemu_branches"], dict)):
        raise InputError("selection policy needs a default profile and branch map")
    for branch, profile_id in policy["qemu_branches"].items():
        require_string(branch, "policy branch")
        if type(profile_id) is not int or profile_id <= 0:
            raise InputError("policy branch mappings must name positive profile IDs")

    branches = {"qemu": set(), "simulator": set()}
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
    validate_profile(selected)
    selected_branch = selected["qemu"]["branch"]
    if selected_branch not in policy["qemu_branches"]:
        raise InputError("selected profile has no supported QEMU branch policy")
    if qemu_branch is not None and selected_branch != qemu_branch:
        raise InputError("selected profile conflicts with the required QEMU target branch")
    simulator_branch = next(iter(branches["simulator"]), simulator_branch)
    require_string(simulator_branch, "simulator target branch")
    # Keep S2a's complete report, including non-QEMU projects. S2c owns which
    # source inputs affect a QEMU-only artifact key; selection must not drop them.
    resolved = {
        "schema_version": 1,
        "sources": {
            "qemu": {"revision": git_head(qemu_dir), "target_branch": selected_branch},
            "simulator": {"revision": git_head(simulator_dir), "target_branch": simulator_branch},
        },
        "profile": selected,
        "dependencies": copy.deepcopy(report["dependencies"]),
    }
    for name, source in resolved["sources"].items():
        require_revision(source["revision"], name + ".revision")
    json.dumps(resolved, allow_nan=False)
    return resolved


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise InputError("duplicate JSON field: " + key)
        result[key] = value
    return result


def read_json(path):
    with open(path, encoding="utf-8") as stream:
        return json.load(stream, object_pairs_hook=unique_object)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resolve-dependencies", required=True, help="S2a dependency application report")
    parser.add_argument("--profiles", default=Path(__file__).parent / "versions/configs.json")
    parser.add_argument("--selection-policy", default=Path(__file__).parent / "versions/qemu-build-policy.json")
    parser.add_argument("--profile-id", type=int, help="explicit compatible configuration ID")
    parser.add_argument("--simulator-branch", default="master")
    parser.add_argument("--qemu-dir", required=True)
    parser.add_argument("--simulator-dir", required=True)
    args = parser.parse_args()
    try:
        resolved = resolve_inputs(read_json(args.resolve_dependencies), read_json(args.profiles),
                                  read_json(args.selection_policy), args.qemu_dir, args.simulator_dir,
                                  args.profile_id, args.simulator_branch)
        print("INFO selected profile {} for QEMU {} at {}; simulator {} at {}".format(
            resolved["profile"]["id"], resolved["sources"]["qemu"]["target_branch"],
            resolved["sources"]["qemu"]["revision"], resolved["sources"]["simulator"]["target_branch"],
            resolved["sources"]["simulator"]["revision"]), file=sys.stderr)
        print("INFO effective profile " + json.dumps(resolved["profile"], sort_keys=True), file=sys.stderr)
        print(json.dumps(resolved, sort_keys=True, allow_nan=False))
        return 0
    except (OSError, ValueError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
