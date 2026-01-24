# BNG-0001: Network Identity and Rebranding

**Status**: accepted
**Created**: 2026-01-17

## Abstract
This document outlines the proposal for the network identity of Bitcoin-NG (BNG), including magic bytes, address space (prefixes), and default ports to ensure network isolation and prevent replay attacks or confusion with Bitcoin Core.

## Motivation
When forking Bitcoin Core, it is crucial to establish a distinct network identity. This prevents:
- Accidental cross-network connections (P2P isolation).
- Address confusion (wallet safety).
- Replay attacks.

## Specification

### 1. Network Magic (P2P Message Start)
Unique magic bytes are required to isolate the BNG P2P network.
As requested, we use branded magic bytes for easier identification.

**Mainnet**:
- Value: `0x42 0x4e 0x47 0x01` ("BNG\x01")

**Testnet**:
- Value: `0x42 0x4e 0x47 0x02` ("BNG\x02")

**Regtest**:
- Value: `0x42 0x4e 0x47 0x03` ("BNG\x03")

### 2. Address Space & Version Bytes
Distinct prefixes prevent BNG addresses from being treated as valid BTC addresses.

- **Legacy (Base58)**:
    - **Mainnet**:
        - P2PKH: `38` (0x26)
        - P2SH: `23` (0x17)
        - WIF (Secret Key): `166` (0xA6)
        - HD Ext Pub (xpub): `0x045f1cf6`
        - HD Ext Prv (xprv): `0x045f18bc`
    - **Testnet**:
        - P2PKH: `100` (0x64)
        - P2SH: `110` (0x6E)
        - WIF (Secret Key): `228` (0xE4)
        - HD Ext Pub (tpub): `0x04351234`
        - HD Ext Prv (tprv): `0x04354321`

- **SegWit (Bech32)**:
    - Mainnet HRP: `bng`
    - Testnet HRP: `tbng`
    - Regtest HRP: `bngr`

### 3. Default Ports
Unique ports to avoid conflict with running Bitcoin nodes.
- **Mainnet**:
    - P2P: `9333`
    - RPC: `9332`
- **Testnet**:
    - P2P: `19333`
    - RPC: `19332`
- **Regtest**:
    - P2P: `19444`
    - RPC: `19443`

### 4. Genesis Block
A new genesis block must be generated to ensure a distinct chain identity.

- **Timestamp Message (coinbase)**: "Yahoo Finance 23/Jan/2026 Bitcoin mining companies make major shift impacting AI and energy markets"

- **Mainnet**
  - Genesis Hash: `cb10278e39a3f83a1cbe12a6fcd6c515e9693e076a945afd8f15fcac39ea4d53`
  - Merkle Root: `5ca903eec1c654c8925c416e2612bdf8893d8b4b911286f3d2af3f9137b72b45`

- **Testnet**
  - Genesis Hash: `3d62949ffc5368f6ebb16ae6524c40d5b479e9fa95c737a89d10421fc9840a18`
  - Merkle Root: `5ca903eec1c654c8925c416e2612bdf8893d8b4b911286f3d2af3f9137b72b45`

- **Regtest**
  - Genesis Hash: `365a78fd5d3a8c3363615cedc6b823936cf1d03136e476277a4890e07210dfba`
  - Merkle Root: `5ca903eec1c654c8925c416e2612bdf8893d8b4b911286f3d2af3f9137b72b45`

See also:
- `test/regtest-init-regression.md` for the unit-test bootstrap gotcha around future-dated genesis header timestamps.
- Boost suite `bng_regtest_init_tests/*` for the black-box regtest init regression coverage.

---
*Original discussion content from `doc/bng/discussion/magic_bytes.md` follows:*

1) Why “magic bytes” are important
A) P2P network “message start” (a.k.a. network magic)
...
(Content from original discussion)
...
