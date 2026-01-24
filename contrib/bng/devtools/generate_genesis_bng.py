#!/usr/bin/env python3

import argparse
import os
import subprocess
import sys
from io import BytesIO


def _repo_root_from_this_file() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _import_test_framework(repo_root: str):
    # Allows importing test_framework/* from within contrib/bng/devtools.
    sys.path.insert(0, os.path.join(repo_root, "test", "functional"))

    from test_framework.messages import (  # noqa: E402
        COIN,
        CBlockHeader,
        COutPoint,
        CTransaction,
        CTxIn,
        CTxOut,
        hash256,
    )

    return {
        "COIN": COIN,
        "CBlockHeader": CBlockHeader,
        "COutPoint": COutPoint,
        "CTransaction": CTransaction,
        "CTxIn": CTxIn,
        "CTxOut": CTxOut,
        "hash256": hash256,
    }


def _scriptnum_push(n: int) -> bytes:
    # Minimal encoding for positive script numbers, matching CScript << int.
    if n == 0:
        return b""
    out = bytearray()
    while n:
        out.append(n & 0xFF)
        n >>= 8
    if out[-1] & 0x80:
        out.append(0)
    return bytes(out)


def _pushdata(data: bytes) -> bytes:
    l = len(data)
    if l < 0x4c:
        return bytes([l]) + data
    if l <= 0xFF:
        return b"\x4c" + bytes([l]) + data
    if l <= 0xFFFF:
        return b"\x4d" + l.to_bytes(2, "little") + data
    return b"\x4e" + l.to_bytes(4, "little") + data


def _make_genesis_coinbase_scriptsig(psz_timestamp: bytes) -> bytes:
    # Matches C++: CScript() << 486604799 << CScriptNum(4) << pszTimestamp
    # 486604799 == 0x1d00ffff is pushed as a script number.
    part_bits = _pushdata(_scriptnum_push(486604799))
    part_4 = _pushdata(_scriptnum_push(4))
    part_msg = _pushdata(psz_timestamp)
    return part_bits + part_4 + part_msg


def _make_coinbase_tx(tf, psz_timestamp: str, genesis_reward_sats: int, script_pub_key_hex: str) -> "object":
    psz_bytes = psz_timestamp.encode("utf-8")
    script_sig = _make_genesis_coinbase_scriptsig(psz_bytes)

    tx = tf["CTransaction"]()
    tx.version = 1
    tx.vin = [tf["CTxIn"](tf["COutPoint"](0, 0xFFFFFFFF), scriptSig=script_sig, nSequence=0xFFFFFFFF)]
    tx.vout = [tf["CTxOut"](genesis_reward_sats, bytes.fromhex(script_pub_key_hex))]
    tx.nLockTime = 0
    tx.rehash()
    return tx


def _uint256_int_to_hex_be(v: int) -> str:
    return v.to_bytes(32, "little")[::-1].hex()


def _find_bitcoin_util(repo_root: str, override: str | None) -> str:
    if override:
        return override
    candidate = os.path.join(repo_root, "build", "bin", "bitcoin-util")
    if os.path.exists(candidate) and os.access(candidate, os.X_OK):
        return candidate
    return "bitcoin-util"


def _grind_header(bitcoin_util: str, header_hex: str) -> str:
    # Uses Bitcoin Core's multithreaded grind implementation.
    out = subprocess.check_output([bitcoin_util, "grind", header_hex], stderr=subprocess.STDOUT, text=True).strip()
    if not out or any(c not in "0123456789abcdefABCDEF" for c in out):
        raise RuntimeError(f"Unexpected bitcoin-util grind output: {out!r}")
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate reproducible BNG genesis block parameters.")
    parser.add_argument("--network", choices=["mainnet", "testnet", "testnet4", "signet"], default="mainnet")
    parser.add_argument("--timestamp", required=False)
    parser.add_argument("--time", type=int, required=False, help="nTime (unix epoch)")
    parser.add_argument("--bits", type=lambda s: int(s, 0), required=False, help="nBits (compact), e.g. 0x1d00ffff")
    parser.add_argument("--version", type=int, default=1)
    parser.add_argument("--reward", type=int, default=50, help="Genesis reward (whole coins)")
    parser.add_argument("--script-pub-key", default="51", help="Output scriptPubKey as hex (default OP_TRUE == 0x51)")
    parser.add_argument("--bitcoin-util", default=None, help="Path to bitcoin-util (defaults to build/bin/bitcoin-util or PATH)")
    args = parser.parse_args()

    repo_root = _repo_root_from_this_file()
    tf = _import_test_framework(repo_root)

    defaults = {
        "mainnet": {
            "timestamp": "Bitcoin-NG 22/Jan/2026 Network Identity genesis",
            "time": 1769040000,
            "bits": 0x1d00ffff,
        },
        "testnet": {
            "timestamp": "Bitcoin-NG testnet 22/Jan/2026 Network Identity genesis",
            "time": 1769083200,
            "bits": 0x1d00ffff,
        },
        "testnet4": {
            "timestamp": "Bitcoin-NG testnet4 22/Jan/2026 Network Identity genesis",
            "time": 1769083201,
            "bits": 0x1d00ffff,
        },
        "signet": {
            "timestamp": "Bitcoin-NG signet 22/Jan/2026 Network Identity genesis",
            "time": 1769083202,
            # Keep Bitcoin signet-style nBits default unless overridden.
            "bits": 0x1e0377ae,
        },
    }

    cfg = defaults[args.network]
    psz_timestamp = args.timestamp or cfg["timestamp"]
    n_time = args.time if args.time is not None else cfg["time"]
    n_bits = args.bits if args.bits is not None else cfg["bits"]

    genesis_reward_sats = args.reward * tf["COIN"]

    tx = _make_coinbase_tx(tf, psz_timestamp, genesis_reward_sats, args.script_pub_key)

    header = tf["CBlockHeader"]()
    header.nVersion = args.version
    header.hashPrevBlock = 0
    header.hashMerkleRoot = tx.txid_int
    header.nTime = n_time
    header.nBits = n_bits
    header.nNonce = 0

    header_hex = header.serialize().hex()

    bitcoin_util = _find_bitcoin_util(repo_root, args.bitcoin_util)
    ground_hex = _grind_header(bitcoin_util, header_hex)

    grounded = tf["CBlockHeader"]()
    grounded.deserialize(BytesIO(bytes.fromhex(ground_hex)))

    print(f"network: {args.network}")
    print(f"pszTimestamp: {psz_timestamp}")
    print(f"nTime: {grounded.nTime}")
    print(f"nBits: 0x{grounded.nBits:08x}")
    print(f"nNonce: {grounded.nNonce}")
    print(f"genesis hash: {grounded.hash_hex}")
    print(f"merkle root: {_uint256_int_to_hex_be(grounded.hashMerkleRoot)}")
    print(f"coinbase txid: {tx.txid}")
    print(f"coinbase scriptSig: {tx.vin[0].scriptSig.hex()}")
    print(f"coinbase scriptPubKey: {tx.vout[0].scriptPubKey.hex()}")
    print("\nC++ snippet:")
    print(f"    const char* pszTimestamp = \"{psz_timestamp}\";")
    print("    const CScript genesisOutputScript = CScript() << OP_TRUE;")
    print(f"    genesis = CreateGenesisBlock(pszTimestamp, genesisOutputScript, {grounded.nTime}, {grounded.nNonce}, 0x{grounded.nBits:08x}, {grounded.nVersion}, 50 * COIN);")
    print(f"    consensus.hashGenesisBlock = genesis.GetHash();")
    print(f"    assert(consensus.hashGenesisBlock == uint256{{\"{grounded.hash_hex}\"}});")
    print(f"    assert(genesis.hashMerkleRoot == uint256{{\"{_uint256_int_to_hex_be(grounded.hashMerkleRoot)}\"}});")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
