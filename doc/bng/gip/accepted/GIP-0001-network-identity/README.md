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

**Proposal:**
- **Mainnet**: `0xf9 0xbe 0xb4 0xd9` (BTC) -> **[TO BE DETERMINED]** (Unique 4 bytes)
- **Testnet**: Distinct value.
- **Regtest**: Distinct value.

### 2. Address Space & Version Bytes
Distinct prefixes prevent BNG addresses from being treated as valid BTC addresses.

- **Legacy (Base58)**:
    - P2PKH: Unique version byte.
    - P2SH: Unique version byte.
    - WIF (Secret Key): Unique version byte.
    - HD Keys (xpub/xprv): Unique 4-byte versions.
- **SegWit (Bech32)**:
    - Mainnet HRP: `bng` (instead of `bc`)
    - Testnet HRP: `tbng` (instead of `tb`)
    - Regtest HRP: `bngr` (instead of `bcrt`)

### 3. Default Ports
Unique ports to avoid conflict with running Bitcoin nodes.
- **Mainnet**: Change `8333`.
- **Testnet**: Change `18333`.

### 4. Genesis Block
A new genesis block must be generated to ensure a distinct chain identity.

---
*Original discussion content from `doc/bng/discussion/magic_bytes.md` follows:*

In Bitcoin Core–style codebases there are a few different “magic bytes” (or magic constants) that serve different purposes. They matter because they’re the first line of defense against your nodes accidentally talking to Bitcoin (or other forks) and because they define how humans and wallets recognize your addresses/keys.

1) Why “magic bytes” are important
A) P2P network “message start” (a.k.a. network magic)
...
(Content from original discussion)
...
