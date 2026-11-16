"""Exercise the installer entry point without contacting a container engine."""
import json
import os
from pathlib import Path
import pty
import shutil
import subprocess
import sys
import tempfile
import unittest


INSTALLER = Path(__file__).with_name("install_and_start_elk.sh")
CERTIFICATES = (
    "ca/ca.crt", "elasticsearch/elasticsearch.crt",
    "elasticsearch/elasticsearch.key", "kibana/kibana.crt", "kibana/kibana.key",
)

# Model only the external commands used by startup. Unknown operations fail,
# so a test cannot silently fall through to the real daemon, Git or network.
COMMAND = r'''
import json
import os
from pathlib import Path
import shutil
import sys

tool, args = Path(sys.argv[0]).name, sys.argv[1:]
state_path = Path(os.environ["ELK_STARTUP_FIXTURE"])
state = json.loads(state_path.read_text())
with state_path.with_suffix(".calls").open("a") as stream:
    stream.write(json.dumps([tool, args]) + "\n")


def finish(code=0):
    state_path.write_text(json.dumps(state))
    raise SystemExit(code)


def fails(operation):
    if state.get("failure") == operation:
        finish(9)


def container(name, identity, mounts=()):
    return {
        "Id": identity, "Name": "/" + name, "State": {"Running": True},
        "Config": {"Labels": {
            "com.docker.compose.project": state["project"],
            "com.docker.compose.project.working_dir": state["elk"] + "/docker-elk",
        }},
        "NetworkSettings": {"Networks": {state["project"] + "_elk": {}}},
        "Mounts": list(mounts),
    }


compose = tool == "podman-compose" or tool == "docker" and args[:1] == ["compose"]
if tool == "docker" and compose:
    args = args[1:]
if compose:
    if args == ["version"]:
        finish()
    if "down" in args:
        fails("down")
        if "filebeat" in state["containers"]:
            finish(8)  # Standalone Filebeat must release the network first.
        state["containers"] = {
            n: c for n, c in state["containers"].items()
            if c["Config"]["Labels"].get("com.docker.compose.project") != state["project"]
        }
        if "-v" in args or "--volumes" in args:
            state["volumes"] = []
        finish()
    if "ps" in args:
        print("NAME STATUS")
        for name, item in state["containers"].items():
            if item["Config"]["Labels"].get("com.docker.compose.project") == state["project"]:
                print(name, "Up" if item["State"]["Running"] else "Exited")
        finish()
    if "up" in args:
        service = args[-1]
        if service == "tls":
            fails("tls")
            for name in state["certificates"]:
                if state.get("failure") == "partial-tls" and name != "ca/ca.crt":
                    continue
                path = Path(state["elk"]) / "docker-elk/tls/certs" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("generated certificate\n")
            finish()
        if service == "setup":
            fails("setup")
            state["volumes"].append(state["project"] + "_elasticsearch")
            finish()
        if "-d" in args:
            fails("stack-start")
            state["generation"] += 1
            for name in ("elasticsearch", "kibana", "logstash"):
                state["containers"][name] = container(name, name + str(state["generation"]))
            finish()
    raise RuntimeError("Unexpected compose operation: " + repr(args))

if tool in ("docker", "podman"):
    if args[:1] == ["ps"]:
        if "--filter" in args:
            fails("project-query")
            wanted = "com.docker.compose.project=" + state["project"]
            if args[args.index("--filter") + 1] != "label=" + wanted:
                raise RuntimeError("Unscoped project query")
            for item in state["containers"].values():
                if item["Config"]["Labels"].get("com.docker.compose.project") == state["project"]:
                    print(item["Id"])
        elif "--format" in args:
            fails("filebeat-query")
            print("\n".join(state["containers"]))
        else:
            print("\n".join(state["containers"]))
        finish()
    if args[:1] == ["inspect"]:
        fails("filebeat-inspect" if args[1:] == ["filebeat"] else "project-inspect")
        found = []
        for identity in args[1:]:
            found += [c for n, c in state["containers"].items()
                      if identity in (n, c["Id"])]
        if len(found) != len(args[1:]):
            finish(1)
        print(json.dumps(found))
        finish()
    if args[:2] == ["rm", "-f"]:
        fails("filebeat-remove")
        for identity in args[2:]:
            matches = [n for n, c in state["containers"].items()
                       if identity in (n, c["Id"])]
            if not matches:
                finish(1)
            for name in matches:
                del state["containers"][name]
        finish()
    if args[:1] == ["pull"]:
        fails("pull")
        finish()
    if args[:2] == ["volume", "ls"]:
        fails("volume-query")
        print("\n".join(state["volumes"]))
        finish()
    if args[:1] == ["run"]:
        fails("filebeat-setup" if "--rm" in args else "filebeat-start")
        if "--rm" in args:
            finish()
        mounts = []
        for index, arg in enumerate(args):
            if arg == "-v":
                source, destination = args[index + 1].split(":")[:2]
                mounts.append({"Source": source, "Destination": destination})
        state["generation"] += 1
        item = container("filebeat", "filebeat" + str(state["generation"]), mounts)
        item["Config"]["Labels"] = {}  # Legacy Filebeat is outside Compose.
        item["State"]["Running"] = state.get("failure") != "daemon-exits"
        state["containers"]["filebeat"] = item
        finish()
    if args[:1] == ["exec"]:
        if args[1:5] != ["filebeat", "filebeat", "test", "output"]:
            raise RuntimeError("Unexpected exec: " + repr(args))
        fails("output")
        finish()
    raise RuntimeError("Unexpected engine operation: " + repr(args))

if tool == "git":
    if args[:1] == ["clone"]:
        shutil.copytree(state["upstream"], Path.cwd() / "docker-elk")
    elif args[:1] == ["rev-parse"]:
        print("tls")
    elif args[:1] not in (["fetch"], ["pull"], ["checkout"]):
        raise RuntimeError("Unexpected git operation: " + repr(args))
    finish()
if tool == "systemctl":
    if args == ["list-unit-files"]:
        print("docker.service enabled")
    elif args[:1] not in (["is-enabled"], ["is-active"]) and args != ["--user", "is-active", "--quiet", "podman.socket"]:
        raise RuntimeError("Unexpected service operation: " + repr(args))
    finish()
if tool == "curl":
    url = next(a for a in args if a.startswith(("http://", "https://")))
    if "/_cluster/health" in url:
        print('{"status":"green"}')
    elif "/_node/stats" in url:
        print('{"status":"green","workers":1}')
    elif "/api/saved_objects/_find" in url:
        print('{"saved_objects":[{"id":"fixture-view","attributes":{"title":"filebeat-*"}}]}')
    elif "/runtime_field" in url:
        Path(args[args.index("-o") + 1]).write_text("{}")
        print("201")
    elif any(p in url for p in ("/api/status", "/_security/_authenticate",
                               "/_license/start_basic", "/api/saved_objects/_import")) or url.endswith(":9200"):
        print("{}")
    else:
        raise RuntimeError("Unexpected HTTP request: " + url)
    finish()
if tool in ("sleep", "sync"):
    finish()
raise RuntimeError("Unexpected fixture command: " + tool)
'''


class ElkStartupTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="elk-startup-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.workspace = self.root / "checkout"
        self.elk = self.workspace / "ELK"
        self.logs = self.workspace / "logs"
        self.upstream = self.root / "upstream"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.state_path = self.root / "state.json"
        self.calls_path = self.state_path.with_suffix(".calls")
        self.calls_path.touch()
        # A closed PATH prevents accidental use of a real engine or service tool.
        for name in ("awk", "cat", "chmod", "chown", "cut", "grep", "head", "id", "jq",
                     "mkdir", "mktemp", "rm", "sed", "seq", "tail", "tr", "wc", "xargs"):
            target = shutil.which(name)
            if target is None:
                self.fail("Required existing startup tool is missing: " + name)
            (self.bin / name).symlink_to(target)
        for name in ("git", "systemctl", "curl", "sleep", "sync"):
            self.stub(name)
        self.prepare_checkout()
        (self.upstream / "tls").mkdir(parents=True)
        (self.upstream / "tls/entrypoint.sh").write_text("#!/bin/bash\nexit 0\n")
        (self.upstream / "kibana/config").mkdir(parents=True)
        (self.upstream / "kibana/config/kibana.yml").write_text("server.ssl.enabled: false\n")
        (self.upstream / "docker-compose.yml").write_text("ES_JAVA_OPTS: -Xmx512m -Xms512m\n")
        (self.upstream / ".env").write_text("STACK_VERSION=fixture\n")
        self.project = "elk-fixture"
        self.state = dict(project=self.project, elk=str(self.elk), upstream=str(self.upstream),
                          certificates=CERTIFICATES, containers={}, generation=0,
                          volumes=["unrelated_elasticsearch"], images=["retained-image"])
        self.state["containers"]["filebeat-helper"] = self.metadata(
            "unrelated-id", project="another-project", directory="/unrelated")
        self.save()

    def stub(self, name):
        path = self.bin / name
        path.write_text("#!" + sys.executable + "\n" + COMMAND)
        path.chmod(0o755)

    def prepare_checkout(self):
        self.elk.mkdir(parents=True)
        self.logs.mkdir()
        (self.elk / ".env").write_text("ELASTIC_PASSWORD=fixture-only\n")
        (self.elk / "filebeat.yml").write_text("fixture configuration\n")
        (self.elk / "type_friendly_name.painless").write_text("emit('fixture');\n")
        (self.elk / "eVSSIM_Logs_Dashboard.ndjson").write_text("{}\n")

    def save(self):
        self.state_path.write_text(json.dumps(self.state))

    def read_state(self):
        return json.loads(self.state_path.read_text())

    def calls(self):
        return [json.loads(line) for line in self.calls_path.read_text().splitlines()]

    def compose_calls(self):
        return [args[1:] if tool == "docker" else args for tool, args in self.calls()
                if tool == "podman-compose" or tool == "docker" and args[:1] == ["compose"]]

    def metadata(self, identity, project=None, directory=None, running=True):
        return dict(Id=identity, Name="/" + identity, State={"Running": running},
                    Config={"Labels": {
                        "com.docker.compose.project": project or self.project,
                        "com.docker.compose.project.working_dir": directory or str(self.elk / "docker-elk"),
                    }}, Mounts=[], NetworkSettings={"Networks": {(project or self.project) + "_elk": {}}})

    def survivors(self, running=True, filebeat=True):
        self.state["containers"]["elasticsearch"] = self.metadata("old-elasticsearch", running=running)
        self.state["volumes"].append(self.project + "_elasticsearch")
        if filebeat:
            item = self.metadata("old-filebeat")
            item["Config"]["Labels"] = {}
            item["Mounts"] = [
                {"Source": str(self.logs), "Destination": "/logs"},
                {"Source": str(self.elk / "filebeat.yml"),
                 "Destination": "/usr/share/filebeat/filebeat.yml"},
                {"Source": str(self.elk / "docker-elk/tls/certs/ca"),
                 "Destination": "/usr/share/filebeat/certs/ca"},
                {"Source": str(self.elk / "filebeat-data"),
                 "Destination": "/usr/share/filebeat/data"},
            ]
            self.state["containers"]["filebeat"] = item
        self.save()

    def run_installer(self, runtime="docker", stdin=subprocess.DEVNULL):
        for name in ("docker", "podman", "podman-compose"):
            (self.bin / name).unlink(missing_ok=True)
        for name in (("docker",) if runtime == "docker" else ("podman", "podman-compose")):
            self.stub(name)
        self.save()
        environment = dict(PATH=str(self.bin), HOME=str(self.root), LANG="C.UTF-8",
                           TMPDIR=str(self.root), ELK_STARTUP_FIXTURE=str(self.state_path),
                           COMPOSE_PROJECT_NAME=self.project, PYTHONDONTWRITEBYTECODE="1")
        result = subprocess.run(
            ["/bin/bash", str(INSTALLER), str(self.logs), str(self.elk)],
            env=environment, stdin=stdin, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=25)
        self.assertNotIn("Traceback (most recent call last)", result.stdout, result.stdout)
        self.state = self.read_state()
        return result

    def assert_started(self, result):
        self.assertEqual(result.returncode, 0, result.stdout)
        item = self.state["containers"]["filebeat"]
        self.assertTrue(item["State"]["Running"])
        expected = {"/logs": str(self.logs),
                    "/usr/share/filebeat/filebeat.yml": str(self.elk / "filebeat.yml"),
                    "/usr/share/filebeat/certs/ca": str(self.elk / "docker-elk/tls/certs/ca"),
                    "/usr/share/filebeat/data": str(self.elk / "filebeat-data")}
        launch = next(args for _, args in reversed(self.calls()) if args[:2] == ["run", "-d"])
        mounts = [launch[i + 1].split(":")[:2] for i, arg in enumerate(launch) if arg == "-v"]
        self.assertEqual(expected, {destination: source for source, destination in mounts
                                   if destination in expected})
        self.assertTrue((self.elk / "filebeat-data/registry").is_dir())
        self.assertIn(["exec", "filebeat", "filebeat", "test", "output"],
                      [args[:5] for _, args in self.calls()])
        self.assertIn("filebeat-helper", self.state["containers"])
        self.assertIn("unrelated_elasticsearch", self.state["volumes"])
        self.assertEqual(["retained-image"], self.state["images"])
        for args in self.compose_calls():
            self.assertFalse(set(args) & {"-v", "--volumes", "--rmi"}, args)

    def test_fresh_startup_initializes_tls_storage_and_filebeat(self):
        result = self.run_installer()
        self.assert_started(result)
        self.assertNotIn(["down"], self.compose_calls())
        for service in ("tls", "setup"):
            self.assertIn(["--profile=setup", "up", "--exit-code-from", service, service],
                          self.compose_calls())
        self.assertIn(self.project + "_elasticsearch", self.state["volumes"])

    def test_replaced_checkout_recreates_old_bind_mounts(self):
        self.survivors()
        old_inode = self.logs.stat().st_ino
        self.workspace.rename(self.root / "retired-checkout")
        self.prepare_checkout()
        self.assertNotEqual(old_inode, self.logs.stat().st_ino)
        result = self.run_installer()
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertNotEqual("old-filebeat", self.state["containers"]["filebeat"]["Id"])
        self.assert_started(result)
        self.assertNotEqual("old-elasticsearch", self.state["containers"]["elasticsearch"]["Id"])
        calls = self.calls()
        removal = next(i for i, (_, args) in enumerate(calls)
                       if args == ["rm", "-f", "old-filebeat"])
        down = next(i for i, (_, args) in enumerate(calls) if args == ["compose", "down"])
        self.assertLess(removal, down)
        self.assertIn(self.project + "_elasticsearch", self.state["volumes"])
        self.assertNotIn(["--profile=setup", "up", "--exit-code-from", "setup", "setup"],
                         self.compose_calls())

    def test_stopped_stack_with_missing_filebeat_is_recovered(self):
        self.survivors(running=False, filebeat=False)
        self.assert_started(self.run_installer())
        self.assertIn(["down"], self.compose_calls())

    def test_complete_certificates_are_reused(self):
        shutil.copytree(self.upstream, self.elk / "docker-elk")
        for name in CERTIFICATES:
            path = self.elk / "docker-elk/tls/certs" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("retained certificate\n")
        self.assert_started(self.run_installer())
        self.assertFalse(any("--exit-code-from" in c and c[-1] == "tls" for c in self.compose_calls()))
        for name in CERTIFICATES:
            self.assertEqual("retained certificate\n",
                             (self.elk / "docker-elk/tls/certs" / name).read_text())

    def test_partial_tls_set_is_replaced_after_stopping_owned_stack(self):
        self.survivors()
        shutil.copytree(self.upstream, self.elk / "docker-elk")
        certs = self.elk / "docker-elk/tls/certs"
        (certs / "elasticsearch").mkdir(parents=True)
        (certs / "elasticsearch/elasticsearch.key").write_text("incomplete old key\n")
        self.assert_started(self.run_installer())
        for name in CERTIFICATES:
            self.assertEqual("generated certificate\n", (certs / name).read_text())
        calls = self.compose_calls()
        self.assertLess(calls.index(["down"]),
                        calls.index(["--profile=setup", "up", "--exit-code-from", "tls", "tls"]))

    def test_filebeat_ownership_conflicts_do_not_remove_containers(self):
        for conflict in ("network", "config"):
            with self.subTest(conflict=conflict):
                self.survivors()
                item = self.state["containers"]["filebeat"]
                if conflict == "network":
                    item["NetworkSettings"]["Networks"] = {"another_elk": {}}
                else:
                    config = next(m for m in item["Mounts"]
                                  if m["Destination"] == "/usr/share/filebeat/filebeat.yml")
                    config["Source"] = "/another-checkout/filebeat.yml"
                expected = self.state["containers"].copy()
                start = len(self.calls())
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertEqual(expected, self.state["containers"])
                self.assertFalse(any(args[:1] == ["rm"] or args == ["compose", "down"]
                                     for _, args in self.calls()[start:]))

    def test_other_checkout_in_same_project_is_not_removed(self):
        self.survivors()
        self.state["containers"]["elasticsearch"]["Config"]["Labels"][
            "com.docker.compose.project.working_dir"] = "/another-checkout/docker-elk"
        expected = self.state["containers"].copy()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertEqual(expected, self.state["containers"])

    def test_inspection_failures_do_not_remove_survivors(self):
        for failure in ("project-query", "project-inspect", "filebeat-query", "filebeat-inspect"):
            with self.subTest(failure=failure):
                self.survivors()
                self.state["failure"] = failure
                expected = self.state["containers"].copy()
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertEqual(expected, self.state["containers"])

    def test_required_startup_failures_return_failure(self):
        for failure in ("tls", "partial-tls", "volume-query", "setup", "stack-start",
                        "pull", "filebeat-setup", "filebeat-start", "daemon-exits", "output"):
            with self.subTest(failure=failure):
                # Each failure gets its own checkout and independent runtime state.
                with tempfile.TemporaryDirectory(dir=self.root) as directory:
                    old_workspace, old_elk, old_logs = self.workspace, self.elk, self.logs
                    self.workspace = Path(directory) / "checkout"
                    self.elk, self.logs = self.workspace / "ELK", self.workspace / "logs"
                    self.prepare_checkout()
                    self.state.update(elk=str(self.elk), failure=failure, containers={},
                                      volumes=["unrelated_elasticsearch"])
                    try:
                        result = self.run_installer()
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        self.assertNotIn("is running and its output is reachable", result.stdout)
                    finally:
                        self.workspace, self.elk, self.logs = old_workspace, old_elk, old_logs

    def test_recovery_failures_stop_before_new_stack_start(self):
        for failure in ("filebeat-remove", "down"):
            with self.subTest(failure=failure):
                self.survivors()
                self.state["failure"] = failure
                start = len(self.calls())
                result = self.run_installer()
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertFalse(any("up" in args for _, args in self.calls()[start:]))

    def test_interactive_decline_leaves_existing_containers_untouched(self):
        for runtime in ("docker", "podman"):
            with self.subTest(runtime=runtime):
                self.survivors()
                expected = self.read_state()
                start = len(self.calls())
                master, terminal = pty.openpty()
                try:
                    os.write(master, b"n\n")
                    result = self.run_installer(runtime, stdin=terminal)
                finally:
                    os.close(master)
                    os.close(terminal)
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertIn("Skipping container restart", result.stdout)
                self.assertEqual(expected, self.state)
                self.assertFalse(any(args[:1] == ["rm"] or "down" in args or "up" in args
                                     or args[:1] == ["run"]
                                     for _, args in self.calls()[start:]))

    def test_repeated_startup_keeps_persistent_data_and_unrelated_resources(self):
        self.assert_started(self.run_installer())
        first_id = self.state["containers"]["filebeat"]["Id"]
        volumes = self.state["volumes"].copy()
        self.assert_started(self.run_installer())
        self.assertNotEqual(first_id, self.state["containers"]["filebeat"]["Id"])
        self.assertEqual(volumes, self.state["volumes"])

    def test_podman_fresh_startup_initializes_tls_and_filebeat(self):
        self.assert_started(self.run_installer("podman"))
        self.assertIn(["--profile=setup", "up", "--exit-code-from", "tls", "tls"],
                      self.compose_calls())

    def test_podman_survivors_and_repeatable_startup(self):
        self.survivors(running=False)
        self.assert_started(self.run_installer("podman"))
        self.assertIn(["down"], self.compose_calls())
        first_id = self.state["containers"]["filebeat"]["Id"]
        self.assert_started(self.run_installer("podman"))
        self.assertNotEqual(first_id, self.state["containers"]["filebeat"]["Id"])

    def test_missing_tls_initializer_returns_failure(self):
        (self.upstream / "tls/entrypoint.sh").unlink()
        result = self.run_installer()
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertNotIn("filebeat", self.state["containers"])


if __name__ == "__main__":
    unittest.main()
