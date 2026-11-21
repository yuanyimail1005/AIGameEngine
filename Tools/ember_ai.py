#!/usr/bin/env python3
"""Client for the Ember editor's AI control interface (line-delimited JSON over TCP on 127.0.0.1).

Library use:
    from ember_ai import EmberEditor
    with EmberEditor.launch(project="MyGame") as editor:      # starts EmberEditor --headless --ai-server
        editor.call("create_entity", name="Box", components={"MeshRenderer": {"Mesh": "builtin://Cube"}})
        print(editor.call("capture", path="/tmp/shot.png"))

    editor = EmberEditor.connect(port=7820)                    # attach to an editor that is already running

Command line:
    ember_ai.py launch [--project DIR] [--port N]              # start a headless editor and keep it running
    ember_ai.py call <method> ['{"json": "params"}'] [--port N]
    ember_ai.py batch <file.jsonl> [--port N]                  # send each line, print responses
    ember_ai.py commands [--port N]                            # list available commands

Only the Python standard library is required.
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import time

DEFAULT_PORT = 7820
REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))


class EmberError(RuntimeError):
    """Raised when a command returns an error."""


class EmberEditor:
    def __init__(self, sock, process=None):
        self._sock = sock
        self._file = sock.makefile("r", encoding="utf-8")
        self._process = process
        self._next_id = 1

    # ------------------------------------------------------------------------------------------
    @classmethod
    def connect(cls, port=DEFAULT_PORT, host="127.0.0.1", timeout=30.0):
        deadline = time.time() + timeout
        last_error = None
        while time.time() < deadline:
            try:
                sock = socket.create_connection((host, port), timeout=600)
                return cls(sock)
            except OSError as error:
                last_error = error
                time.sleep(0.2)
        raise ConnectionError(f"cannot connect to the Ember editor on {host}:{port}: {last_error}")

    @classmethod
    def launch(cls, project=None, port=0, editor=None, extra_args=None, timeout=120.0):
        """Starts a headless editor with the AI server and connects to it. port=0 picks a free port."""
        editor = editor or find_editor()
        args = [editor, "--headless", "--ai-server", str(port)]
        if project:
            args += ["--project", str(project)]
        args += list(extra_args or [])
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = process.stdout.readline()
            if not line:
                if process.poll() is not None:
                    raise RuntimeError(f"EmberEditor exited with code {process.returncode}")
                continue
            if line.startswith("EMBER_AI_SERVER_READY"):
                actual_port = int(line.split("port=")[1])
                instance = cls.connect(actual_port)
                instance._process = process
                instance.port = actual_port
                # Keep draining the editor's output so it never blocks on a full pipe.
                import threading
                threading.Thread(target=lambda: [None for _ in process.stdout], daemon=True).start()
                return instance
        process.kill()
        raise TimeoutError("EmberEditor did not start its AI server in time")

    # ------------------------------------------------------------------------------------------
    def request(self, method, params=None):
        """Sends a request and returns the full response object (never raises for command errors)."""
        request_id = self._next_id
        self._next_id += 1
        payload = json.dumps({"id": request_id, "method": method, "params": params or {}})
        self._sock.sendall(payload.encode("utf-8") + b"\n")
        line = self._file.readline()
        if not line:
            raise ConnectionError("the editor closed the connection")
        return json.loads(line)

    def call(self, method, **params):
        """Calls a command and returns its result; raises EmberError on failure."""
        response = self.request(method, params)
        if "error" in response:
            raise EmberError(f"{method}: {response['error']['message']}")
        return response.get("result")

    def close(self, quit_editor=True):
        if self._process is not None and quit_editor:
            try:
                self.call("quit")
            except Exception:
                pass
            try:
                self._process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self._process.kill()
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def find_editor():
    """Locates the EmberEditor executable (EMBER_EDITOR env var, then common build directories)."""
    if os.environ.get("EMBER_EDITOR"):
        return os.environ["EMBER_EDITOR"]
    name = "EmberEditor.exe" if os.name == "nt" else "EmberEditor"
    for build in ("build", "build-release", "build/Release", "build/Debug"):
        candidate = os.path.join(REPO_ROOT, build, "bin", name)
        if os.path.isfile(candidate):
            return candidate
    raise FileNotFoundError("EmberEditor not found; build it or set EMBER_EDITOR")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    sub = parser.add_subparsers(dest="command", required=True)
    launch = sub.add_parser("launch", help="start a headless editor with the AI server (runs until quit)")
    launch.add_argument("--project")
    call = sub.add_parser("call", help="call one command")
    call.add_argument("method")
    call.add_argument("params", nargs="?", default="{}")
    batch = sub.add_parser("batch", help="send every line of a JSONL file")
    batch.add_argument("file")
    sub.add_parser("commands", help="list commands")
    args = parser.parse_args()

    if args.command == "launch":
        editor_args = [find_editor(), "--headless", "--ai-server", str(args.port)]
        if args.project:
            editor_args += ["--project", args.project]
        os.execv(editor_args[0], editor_args)

    editor = EmberEditor.connect(args.port)
    try:
        if args.command == "call":
            print(json.dumps(editor.request(args.method, json.loads(args.params)), indent=2))
        elif args.command == "batch":
            failures = 0
            with open(args.file) as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith("#"):
                        continue
                    request = json.loads(line)
                    response = editor.request(request["method"], request.get("params"))
                    print(json.dumps(response))
                    failures += "error" in response
            sys.exit(1 if failures else 0)
        elif args.command == "commands":
            for command in editor.call("list_commands"):
                print(f"{command['name']:24} {command['description']}")
    finally:
        editor.close(quit_editor=False)


if __name__ == "__main__":
    main()
