#!/usr/bin/env python3
# Copyright (c) 2014-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Exercise the planned BNG-to-CTAM mining client contract.

This test starts a sibling CTAM REST server, drives it through a small BNG-side
client wrapper, and verifies the branch invariant that matters for mining:
only the winning branch is committed, and CTAM clears all other branches when
that commit happens.

It also smoke-tests the existing `/crypto/cta_sha256` endpoint as the first
mock external `csha256` implementation path.
"""

from __future__ import annotations

from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin
from urllib.request import Request, urlopen

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal


class CtamRestClient:
    def __init__(self, base_url: str):
        self.base_url = base_url if base_url.endswith("/") else f"{base_url}/"

    def request(self, method: str, path: str, payload=None):
        data = None
        headers = {}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["content-type"] = "application/json"
        elif method == "POST":
            data = b""
            headers["content-type"] = "text/plain"
        req = Request(urljoin(self.base_url, path), data=data, headers=headers, method=method)
        with urlopen(req, timeout=5) as response:
            body = response.read()
            try:
                return json.loads(body.decode("utf-8"))
            except json.JSONDecodeError:
                return body

    def wait_for_server(self, retries: int = 80, delay: float = 0.25) -> bool:
        for _ in range(retries):
            try:
                health = self.health()
                if isinstance(health, dict) and health.get("test") == "ok":
                    return True
            except (HTTPError, URLError, TimeoutError):
                pass
            time.sleep(delay)
        return False

    def health(self):
        return self.request("GET", "health")

    def status(self):
        return self.request("GET", "status")

    def list_branches(self):
        return self.request("GET", "branches")

    def get_branch(self, branch_key: int):
        return self.request("GET", f"branches/{branch_key}")

    def create_branch(self):
        response = self.request("POST", "branches")
        return response["new_branch_key"]

    def commit_branch(self, branch_key: int):
        return self.request("POST", f"branches/{branch_key}/commit")

    def branch_cxor(self, branch_key: int, indices: list[int], context_size: int):
        payload = {
            "context_size": context_size,
            "stream": [self._bit_indices_to_stream_hex(indices, context_size)],
        }
        return self.request("POST", f"branches/{branch_key}/cxor", payload)

    def cta_sha256(self, *, message_hex: str, injections: int, keep_branch: bool = False):
        return self.request(
            "POST",
            "crypto/cta_sha256",
            {
                "message_hex": message_hex,
                "injections": injections,
                "keep_branch": keep_branch,
            },
        )

    @staticmethod
    def _bit_indices_to_stream_hex(indices: list[int], context_size: int) -> str:
        num_bits = 1 << context_size
        stream = bytearray((num_bits + 7) // 8)
        for index in indices:
            if index < 0 or index >= num_bits:
                raise ValueError(f"bit index {index} out of range for context_size={context_size}")
            byte_index = index // 8
            bit_index = index % 8
            stream[byte_index] |= 1 << bit_index
        return stream.hex()


class InterfaceCtamRestMiningTest(BitcoinTestFramework):
    CTAM_HOST = "127.0.0.1"
    CTAM_PORT = 8080

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 0

    def skip_test_if_missing_module(self):
        ctam_repo = self._ctam_repo()
        ctam_binary = ctam_repo / "target" / "debug" / "ctam"
        if not ctam_repo.is_dir():
            raise self.SkipTest(f"CTAM repo not found at {ctam_repo}")
        if shutil.which("cargo") is None and not (ctam_binary.exists() and os.access(ctam_binary, os.X_OK)):
            raise self.SkipTest("cargo or a built ctam binary is required")
        if self._port_in_use(self.CTAM_HOST, self.CTAM_PORT):
            raise self.SkipTest(f"{self.CTAM_HOST}:{self.CTAM_PORT} is already in use")

    def setup_network(self):
        self.nodes = []

    @property
    def SkipTest(self):
        from test_framework.test_framework import SkipTest

        return SkipTest

    def _ctam_repo(self) -> Path:
        default_repo = Path(__file__).resolve().parents[4] / "ctam"
        return Path(os.environ.get("CTAM_REPO_ROOT", default_repo))

    def _ctam_command(self, state_path: str) -> list[str]:
        ctam_repo = self._ctam_repo()
        ctam_binary = ctam_repo / "target" / "debug" / "ctam"
        if ctam_binary.exists() and os.access(ctam_binary, os.X_OK):
            return [str(ctam_binary), "--serve", "rest", "--state-path", state_path]
        return [
            "cargo",
            "run",
            "--manifest-path",
            str(ctam_repo / "Cargo.toml"),
            "--",
            "--serve",
            "rest",
            "--state-path",
            state_path,
        ]

    @staticmethod
    def _port_in_use(host: str, port: int) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.25)
            return sock.connect_ex((host, port)) == 0

    @contextmanager
    def _running_ctam_server(self):
        with tempfile.TemporaryDirectory(prefix="ctam-state-") as state_dir:
            log_path = Path(state_dir) / "ctam_server.log"
            command = self._ctam_command(state_dir)
            client = CtamRestClient(f"http://{self.CTAM_HOST}:{self.CTAM_PORT}/")
            env = os.environ.copy()
            env.setdefault("RUST_LOG", "info")

            with open(log_path, "wb") as log_file:
                process = subprocess.Popen(
                    command,
                    cwd=str(self._ctam_repo()),
                    env=env,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                )
                try:
                    if not client.wait_for_server():
                        process.terminate()
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            process.kill()
                        log_file.flush()
                        log_tail = Path(log_path).read_text(encoding="utf-8", errors="replace").splitlines()[-20:]
                        raise AssertionError(
                            "CTAM server failed to start. "
                            f"command={' '.join(command)}\n"
                            f"Last log lines:\n{'\n'.join(log_tail)}"
                        )
                    yield client
                finally:
                    if process.poll() is None:
                        process.send_signal(signal.SIGINT)
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            process.terminate()
                            try:
                                process.wait(timeout=2)
                            except subprocess.TimeoutExpired:
                                process.kill()

    def _assert_http_error(self, callable_obj, expected_status: int):
        try:
            callable_obj()
        except HTTPError as error:
            assert_equal(error.code, expected_status)
            return
        raise AssertionError(f"expected HTTP {expected_status}")

    def run_test(self):
        with self._running_ctam_server() as ctam:
            self.log.info("Probe CTAM status and context size")
            health = ctam.health()
            assert_equal(health["test"], "ok")
            context_size = health["context_size"]
            status = ctam.status()
            assert_equal(status["status"], "running")
            assert_equal(status["trunk_key"], 0)
            assert_equal(status["branch_count"], 0)

            self.log.info("Smoke-test the mock external CTA-SHA256 endpoint")
            digest = ctam.cta_sha256(message_hex="616263", injections=1, keep_branch=False)
            assert_equal(digest["branch_kept"], False)
            assert_equal(digest["injections"], 1)
            assert_equal(len(digest["digest_hex"]), 64)

            self.log.info("Create two competing candidate branches from BNG-side code")
            winning_branch = ctam.create_branch()
            losing_branch = ctam.create_branch()
            assert_equal(len(ctam.list_branches()["branches"]), 2)

            self.log.info("Replay separate CXOR work on each candidate branch")
            winner_result = ctam.branch_cxor(winning_branch, [0, 1, 5, 13], context_size)
            loser_result = ctam.branch_cxor(losing_branch, [2, 3, 8], context_size)
            assert_equal(winner_result["branch_key"], winning_branch)
            assert_equal(loser_result["branch_key"], losing_branch)
            assert_equal(ctam.status()["branch_count"], 2)

            self.log.info("Commit only the winning branch")
            commit_result = ctam.commit_branch(winning_branch)
            assert_equal(commit_result["status"], "committed")
            committed_status = ctam.status()
            assert_equal(committed_status["trunk_key"], winning_branch)
            assert_equal(committed_status["branch_count"], 0)
            assert_equal(ctam.list_branches()["branches"], [])

            self.log.info("Ensure the losing branch was never committed")
            self._assert_http_error(lambda: ctam.get_branch(losing_branch), 404)
            self._assert_http_error(lambda: ctam.commit_branch(losing_branch), 409)


if __name__ == '__main__':
    InterfaceCtamRestMiningTest(__file__).main()