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

- **Timestamp Message**: "Bitcoin-NG: A new era of scalability 2026-01-23"
- **Genesis Hash**: [Pending Generation]
- **Merkle Root**: [Pending Generation]

---
*Original discussion content from `doc/bng/discussion/magic_bytes.md` follows:*

1) Why “magic bytes” are important
A) P2P network “message start” (a.k.a. network magic)
...
(Content from original discussion)
...
