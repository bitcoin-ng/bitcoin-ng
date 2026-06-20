#!/usr/bin/env python3
# Copyright (c) 2014-present The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end CTAM REST mining lifecycle tests."""

from __future__ import annotations

from contextlib import contextmanager
from decimal import Decimal
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
from urllib.parse import urljoin, urlparse
from urllib.request import Request, urlopen

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import assert_equal


MINOR_CTAM_DIFFICULTY_ROUNDS = 16
FF_BLOCKCHAIN_DATA_FILENAME = "blocks.json"


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
            data = b"{}"
            headers["content-type"] = "application/json"
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

    def cta_sha256(self, *, header_hex: str, energy_difficulty: int, context_size: int, clone_from=None):
        payload = {
            "payload_hex": header_hex,
            "energy_difficulty": energy_difficulty,
            "major_ctam_difficulty_context_size": context_size,
            "minor_ctam_difficulty_rounds": MINOR_CTAM_DIFFICULTY_ROUNDS,
            "injections": MINOR_CTAM_DIFFICULTY_ROUNDS,
            "keep_branch": True,
        }
        if clone_from is not None:
            payload["clone_from"] = clone_from
        return self.request("POST", "crypto/cta_sha256", payload)

    def commit_branch(self, branch_key: int, *, bng_height: int, bng_block_hash: str):
        return self.request(
            "POST",
            f"branches/{branch_key}/commit",
            {
                "bng_height": bng_height,
                "bng_block_hash": bng_block_hash,
            },
        )


class CtamMiningFlowsTest(BitcoinTestFramework):
    CTAM_HOST = "127.0.0.1"
    CTAM_PORT = 8080
    CTAM_URL = f"http://{CTAM_HOST}:{CTAM_PORT}/"
    CTAM_REST_URL_ENV = "CTAM_REST_URL"
    TEST_SELECTOR_ENV = "CTAM_MINING_FLOW_TEST"

    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [self.ctam_node_args()] * self.num_nodes
        self.uses_wallet = None
        self.supports_cli = False

    def skip_test_if_missing_module(self):
        if self.ctam_server_is_available():
            return

        ctam_repo = self.ctam_repo()
        ctam_binary = ctam_repo / "target" / "debug" / "ctam"
        if not ctam_repo.is_dir():
            raise SkipTest(f"CTAM repo not found at {ctam_repo}")
        if shutil.which("cargo") is None and not (ctam_binary.exists() and os.access(ctam_binary, os.X_OK)):
            raise SkipTest("cargo or a built ctam binary is required")
        parsed_url = urlparse(self.ctam_url())
        if self.port_in_use(parsed_url.hostname, parsed_url.port):
            raise SkipTest(f"{parsed_url.hostname}:{parsed_url.port} is already in use")

    def setup_network(self):
        self.add_nodes(self.num_nodes, self.extra_args)

    @classmethod
    def ctam_url(cls):
        return os.environ.get(cls.CTAM_REST_URL_ENV, cls.CTAM_URL)

    @classmethod
    def using_external_ctam_server(cls):
        return cls.CTAM_REST_URL_ENV in os.environ

    def ctam_server_is_available(self):
        if self.using_external_ctam_server():
            return True

        parsed_url = urlparse(self.ctam_url())
        if not self.port_in_use(parsed_url.hostname, parsed_url.port):
            return False

        try:
            health = CtamRestClient(self.ctam_url()).health()
        except (HTTPError, URLError, TimeoutError, OSError, ValueError):
            return False
        return isinstance(health, dict) and health.get("test") == "ok"

    @classmethod
    def ctam_node_args(cls):
        return [
            "-ctammining=1",
            f"-ctamresturl={cls.ctam_url()}",
            "-fallbackfee=0.0002",
        ]

    def ctam_repo(self) -> Path:
        default_repo = Path(__file__).resolve().parents[4] / "ctam"
        return Path(os.environ.get("CTAM_REPO_ROOT", default_repo))

    def ff_blockchain_data_dir(self) -> Path:
        default_dir = Path(__file__).resolve().parent / "data" / "fastforward_blockchain"
        return Path(os.environ.get("CTAM_FF_BLOCKCHAIN_DATA_DIR", default_dir))

    def ff_blockchain_data_path(self) -> Path:
        return self.ff_blockchain_data_dir() / FF_BLOCKCHAIN_DATA_FILENAME

    def ff_blockchain_data_exists(self) -> bool:
        return self.ff_blockchain_data_path().is_file()

    def save_dumped_blocks(self, blocks) -> None:
        path = self.ff_blockchain_data_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps({"blocks": blocks}, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    def load_dumped_blocks(self):
        data_dir = self.ff_blockchain_data_dir()
        if not data_dir.is_dir():
            raise AssertionError(
                "Fast-forward blockchain data directory is missing: "
                f"{data_dir}. Create it and run the genesis scenario to dump block data."
            )
        path = self.ff_blockchain_data_path()
        if not path.is_file():
            raise AssertionError(f"Fast-forward blockchain dump is missing: {path}")
        data = json.loads(path.read_text(encoding="utf-8"))
        blocks = data.get("blocks")
        if not isinstance(blocks, list) or not blocks:
            raise AssertionError(f"Fast-forward blockchain dump has no blocks: {path}")
        for expected_height, block in enumerate(blocks, start=1):
            assert_equal(block["height"], expected_height)
            assert_equal(len(block["header_hex"]), 160)
            assert block["hex"].startswith(block["header_hex"])
            assert_equal(len(block["hash"]), 64)
        return blocks

    @staticmethod
    def mutate_block_hex(block_hex: str) -> str:
        raw = bytearray.fromhex(block_hex)
        header_size = 80
        if len(raw) <= header_size:
            raise AssertionError("cannot mutate a block that has no body beyond the header")
        # Flip a bit in the block body so the header merkle root no longer matches.
        raw[header_size] ^= 0x01
        return raw.hex()

    def ctam_command(self, state_path: str) -> list[str]:
        ctam_repo = self.ctam_repo()
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
    def port_in_use(host: str, port: int) -> bool:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.25)
            return sock.connect_ex((host, port)) == 0

    @contextmanager
    def running_ctam_server(self):
        if self.ctam_server_is_available():
            client = CtamRestClient(self.ctam_url())
            if not client.wait_for_server():
                raise AssertionError(f"External CTAM server did not become healthy at {self.ctam_url()}")
            self.log.info("Using externally managed CTAM REST server at %s", self.ctam_url())
            yield client
            return

        with tempfile.TemporaryDirectory(prefix="ctam-state-") as state_dir:
            log_path = Path(state_dir) / "ctam_server.log"
            command = self.ctam_command(state_dir)
            client = CtamRestClient(self.ctam_url())
            env = os.environ.copy()
            env.setdefault("RUST_LOG", "info")

            with open(log_path, "wb") as log_file:
                process = subprocess.Popen(
                    command,
                    cwd=str(self.ctam_repo()),
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
                            f"Last log lines:\n{chr(10).join(log_tail)}"
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

    def start_mining_node(self, index: int):
        self.start_node(index)
        if self.is_wallet_compiled():
            self.nodes[index].createwallet(wallet_name=self.default_wallet_name, load_on_startup=True)
        return self.nodes[index]

    def mining_address(self, node):
        if self.is_wallet_compiled():
            return node.get_wallet_rpc(self.default_wallet_name).getnewaddress()
        return node.get_deterministic_priv_key().address

    def mine_blocks(self, node, block_count: int):
        address = self.mining_address(node)
        start_height = node.getblockcount()
        mined = self.generatetoaddress(node, block_count, address, sync_fun=self.no_op)
        assert_equal(node.getblockcount(), start_height + block_count)
        return mined

    def assert_coinbase_outputs(self, node, block_hashes, *, expected_count: int):
        assert_equal(len(block_hashes), expected_count)
        total_value = Decimal("0")
        for block_hash in block_hashes:
            block = node.getblock(block_hash, 2)
            coinbase_tx = block["tx"][0]
            assert "coinbase" in coinbase_tx["vin"][0]
            total_value += coinbase_tx["vout"][0]["value"]
            node.getblockheader(block_hash)
        assert_equal(total_value, Decimal("50") * expected_count)

    def assert_ctam_empty(self, ctam: CtamRestClient):
        status = ctam.status()
        assert_equal(status["status"], "running")
        assert_equal(status["bng_height"], None)
        assert_equal(status["bng_block_hash"], None)
        assert_equal(status["trunk_key"], 0)
        assert_equal(status["branch_count"], 0)

    def assert_ctam_matches_tip(self, ctam: CtamRestClient, node):
        status = ctam.status()
        assert_equal(status["bng_height"], node.getblockcount())
        assert_equal(status["bng_block_hash"], node.getbestblockhash())
        assert_equal(status["branch_count"], 0)

    def dump_blockchain_from_genesis(self, node):
        blocks = []
        for height in range(1, node.getblockcount() + 1):
            block_hash = node.getblockhash(height)
            block_hex = node.getblock(block_hash, 0)
            header = node.getblockheader(block_hash)
            header_hex = block_hex[:160]
            assert_equal(len(header_hex), 160)
            blocks.append({
                "height": height,
                "hash": block_hash,
                "hex": block_hex,
                "header_hex": header_hex,
                "bits": header["bits"],
            })
        return blocks

    def replay_dumped_headers_to_ctam(self, ctam: CtamRestClient, blocks, *, commit_interval: int = 10):
        tip_height = len(blocks)
        branch_key = None
        last_committed_height = 0

        for block in blocks:
            height = block["height"]
            context_size = ctam.status()["context_size"]
            result = ctam.cta_sha256(
                header_hex=block["header_hex"],
                energy_difficulty=int(block["bits"], 16),
                context_size=context_size,
                clone_from=branch_key,
            )
            assert_equal(result["branch_kept"], True)
            branch_key = int(result["branch_key"])

            if height % commit_interval == 0:
                self.commit_replay_branch(ctam, branch_key, height, block["hash"])
                branch_key = None
                last_committed_height = height

        if tip_height != last_committed_height:
            assert branch_key is not None
            self.commit_replay_branch(ctam, branch_key, tip_height, blocks[-1]["hash"])

    def fast_forward_node_from_dumped_blocks(self, ctam: CtamRestClient, node, blocks):
        branch_key = None

        for block in blocks:
            height = block["height"]
            context_size = ctam.status()["context_size"]
            result = ctam.cta_sha256(
                header_hex=block["header_hex"],
                energy_difficulty=int(block["bits"], 16),
                context_size=context_size,
                clone_from=branch_key,
            )
            assert_equal(result["branch_kept"], True)
            branch_key = int(result["branch_key"])

            self.commit_replay_branch(ctam, branch_key, height, block["hash"])
            branch_key = None

            assert_equal(node.submitblock(block["hex"]), None)
            assert_equal(node.getblockcount(), height)
            assert_equal(node.getbestblockhash(), block["hash"])

    def commit_replay_branch(self, ctam: CtamRestClient, branch_key: int, height: int, block_hash: str):
        result = ctam.commit_branch(branch_key, bng_height=height, bng_block_hash=block_hash)
        assert_equal(result["status"], "committed")
        status = ctam.status()
        assert_equal(status["bng_height"], height)
        assert_equal(status["bng_block_hash"], block_hash)
        assert_equal(status["branch_count"], 0)

    def test_genesis_flow(self):
        self.log.info("Test CTAM-backed mining from empty CTAM state")
        with self.running_ctam_server() as ctam:
            self.assert_ctam_empty(ctam)
            node = self.start_mining_node(0)
            mined = self.mine_blocks(node, 3)

            self.assert_ctam_matches_tip(ctam, node)
            self.assert_coinbase_outputs(node, mined, expected_count=3)
            if self.is_wallet_compiled():
                balances = node.get_wallet_rpc(self.default_wallet_name).getbalances()
                assert_equal(balances["mine"]["immature"], 150)
                assert_equal(len(node.get_wallet_rpc(self.default_wallet_name).listunspent()), 0)

            if not self.ff_blockchain_data_exists():
                self.log.info("Dumping blockchain data for fast-forward test")
                dumped_blocks = self.dump_blockchain_from_genesis(node)
                self.save_dumped_blocks(dumped_blocks)

            self.stop_node(0)

    def test_fast_forward_from_chain_copy(self):
        self.log.info("Load dumped BNG block data from genesis")
        dumped_blocks = self.load_dumped_blocks()
        source_tip = dumped_blocks[-1]["hash"]
        source_height = len(dumped_blocks)

        self.log.info("Replay dumped BNG headers into fresh CTAM and play dumped blocks")
        with self.running_ctam_server() as ctam:
            self.assert_ctam_empty(ctam)
            copied = self.start_mining_node(2)
            assert_equal(copied.getblockcount(), 0)

            self.fast_forward_node_from_dumped_blocks(ctam, copied, dumped_blocks)
            assert_equal(copied.getblockcount(), source_height)
            assert_equal(copied.getbestblockhash(), source_tip)
            self.assert_ctam_matches_tip(ctam, copied)
            status = ctam.status()
            assert status["context_size"] >= status["min_context_size"]

            self.log.info("Mine one more block after client-side fast-forward")
            old_height = copied.getblockcount()
            self.mine_blocks(copied, 1)
            assert_equal(copied.getblockcount(), old_height + 1)
            self.assert_ctam_matches_tip(ctam, copied)
            copied.getblock(copied.getbestblockhash())

            self.stop_node(2)

    def test_fast_forward_block_submission_rejections(self):
        self.log.info("Reject mutated, out-of-order, and duplicate BNG block submissions")
        dumped_blocks = self.load_dumped_blocks()

        with self.running_ctam_server() as ctam:
            self.assert_ctam_empty(ctam)
            node = self.start_mining_node(1)
            try:
                mutated_hex = self.mutate_block_hex(dumped_blocks[0]["hex"])
                mutated_result = node.submitblock(mutated_hex)
                assert mutated_result is not None, "mutated block should be rejected"
                assert_equal(node.getblockcount(), 0)

                out_of_order_result = node.submitblock(dumped_blocks[1]["hex"])
                assert out_of_order_result is not None
                assert_equal(node.getblockcount(), 0)

                skipped_result = node.submitblock(dumped_blocks[2]["hex"])
                assert skipped_result is not None
                assert_equal(node.getblockcount(), 0)

                self.fast_forward_node_from_dumped_blocks(ctam, node, dumped_blocks[:2])
                assert_equal(node.getblockcount(), 2)
                assert_equal(node.getbestblockhash(), dumped_blocks[1]["hash"])
                self.assert_ctam_matches_tip(ctam, node)

                duplicate_result = node.submitblock(dumped_blocks[1]["hex"])
                assert duplicate_result is not None
                assert_equal(node.getblockcount(), 2)
                assert_equal(node.getbestblockhash(), dumped_blocks[1]["hash"])
                self.assert_ctam_matches_tip(ctam, node)
            finally:
                self.stop_node(1)

    def test_mining_fails_without_ctam_fallback(self):
        self.log.info("Restart a miner against an unavailable CTAM REST endpoint")
        bad_url = "http://127.0.0.1:1/"
        self.restart_node(0, extra_args=["-ctammining=1", f"-ctamresturl={bad_url}", "-fallbackfee=0.0002"])
        node = self.nodes[0]
        start_height = node.getblockcount()
        try:
            self.mine_blocks(node, 1)
        except Exception as exc:
            self.log.info("Expected CTAM mining failure: %s", exc)
        else:
            raise AssertionError("mining unexpectedly succeeded without a reachable CTAM server")
        assert_equal(node.getblockcount(), start_height)

    def run_test(self):
        selected_test = os.environ.get(self.TEST_SELECTOR_ENV)
        if selected_test is None:
            self.test_genesis_flow()
            self.test_fast_forward_from_chain_copy()
            self.test_fast_forward_block_submission_rejections()
        elif selected_test == "genesis":
            self.test_genesis_flow()
        elif selected_test == "fast_forward":
            self.test_fast_forward_from_chain_copy()
        elif selected_test == "rejections":
            self.test_fast_forward_block_submission_rejections()
        elif selected_test == "no_fallback":
            self.test_mining_fails_without_ctam_fallback()
        else:
            raise AssertionError(
                f"{self.TEST_SELECTOR_ENV} must be unset, 'genesis', 'fast_forward', 'rejections', or 'no_fallback'; got {selected_test!r}"
            )


if __name__ == "__main__":
    CtamMiningFlowsTest(__file__).main()
