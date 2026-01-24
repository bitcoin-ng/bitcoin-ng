# Signet (BIP325) — tracking note (GIP-0001)

**Date**: 2026-01-24
**Scope**: `src/kernel/chainparams.cpp` signet policy + genesis helper rationale

## What signet is

Signet (“signed net”, BIP325) is a **public test network** that adds one extra consensus rule:

- Blocks must satisfy a “challenge” (see `consensus.signet_challenge`).
- In practice: only entities with the corresponding signing setup can reliably produce valid signet blocks.

This makes signet **more stable/predictable than testnet**, while still being public and long-lived. It is primarily used for integration testing, wallet/node interoperability, and coordinated experiments.

## Policy for BNG

BNG’s rebranding/network identity changes (GIP-0001) apply to the BNG-native networks:

- **MAIN / TESTNET / REGTEST**: BNG magic bytes, ports, address prefixes/HRPs, and a BNG-specific genesis.

Signet is **not** the production network and should not be treated as “BNG mainnet with a toggle”. In this tree we intentionally keep:

- **Bitcoin signet** support for interoperability/developer convenience.

This means `SigNetParams` remains upstream-oriented (Bitcoin signet), unless/until BNG explicitly defines a separate “BNG signet” network.

## Why we keep two genesis helpers

We keep both `CreateGenesisBlock(...)` and `CreateBNGGenesisBlock(...)` because they serve different invariants:

- `CreateGenesisBlock(...)` is the **upstream-compatible** helper, intentionally preserving the original Bitcoin genesis coinbase message/script. This is relied upon by upstream-style networks in this tree (notably **signet** and **testnet4**) that have known genesis hashes/merkle roots and external assumptions.
- `CreateBNGGenesisBlock(...)` is the **BNG-specific** helper, allowing BNG main/testnet/regtest to brand the coinbase timestamp/message without accidentally changing upstream network parameters.

If `CreateGenesisBlock(...)` were branded/changed, the genesis coinbase bytes change, which changes the genesis merkle root (and potentially the genesis hash), breaking signet/testnet4 assertions and upstream expectations.

## If BNG ever wants a “BNG signet”

That would be a separate, explicitly defined network (signet-like) with its own identity. At minimum it would need its own:

- signet challenge, seeds, ports
- address prefixes/HRP
- genesis + asserts
- `chainTxData`, `assumevalid`, `assumeutxo` (initially empty)

In other words: treat it like introducing a new chain type, not as a minor tweak to Bitcoin signet.
