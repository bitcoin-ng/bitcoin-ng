# Implementation Plan - GIP-0001 Network Constants (Code)

This plan details the code changes required to implement the GIP-0001 network identity constants in [src/kernel/chainparams.cpp](file:///z:/home/yy/code/bng/bitcoin-ng/src/kernel/chainparams.cpp).

## User Review Required

> [!WARNING]
> **Genesis Block Reset**: We will be resetting the Genesis Block for Mainnet and Testnet. This is a hard fork / new chain event.
> **Seed Removal**: All existing Bitcoin DNS seeds will be removed.

## Proposed Changes

### Core Consensus & Parameters

#### [MODIFY] [chainparams.cpp](file:///src/kernel/chainparams.cpp)

**General Changes:**
- Update `pchMessageStart` (Magic Bytes) for all networks.
- Update `nDefaultPort` for all networks.
- Update `base58Prefixes` and `bech32_hrp`.
- Clear `vSeeds` and `vFixedSeeds`.
- Reset `consensus.hashGenesisBlock` and `genesis.hashMerkleRoot` assertions (will fail initially, ensuring we catch the new hash).
- Reset `consensus.nMinimumChainWork` and `consensus.defaultAssumeValid` to 0/empty.

**Specific Values (from GIP-0001):**

| Parameter | Mainnet | Testnet | Regtest |
| :--- | :--- | :--- | :--- |
| **Magic** | `0x42, 0x4e, 0x47, 0x01` | `0x42, 0x4e, 0x47, 0x02` | `0x42, 0x4e, 0x47, 0x03` |
| **Port** | `9333` | `19333` | `19444` |
| **HRP** | `bng` | `tbng` | `bngr` |

**Base58 Prefixes (Mainnet):**
- PubKey: `38`
- Script: `23`
- Secret: `166`
- ExtPub: `0x045f1cf6`
- ExtPrv: `0x045f18bc`

**Base58 Prefixes (Testnet):**
- PubKey: `100`
- Script: `110`
- Secret: `228`
- ExtPub: `0x04351234`
- ExtPrv: `0x04354321`

## Verification Plan

### Automated Tests
- **Build**: Ensure code compiles.
- **Unit Tests**: Run `src/test/test_bitcoin` (Expect failures related to address formatting, which is the NEXT task, but basic chainparams instantiation should work).
- **Temporary Genesis Check**: Run the daemon; it should fail to start or assert on the genesis hash. We will capture the new Genesis Hash from the assertion failure or log, and then update the code with the correct hash.

### Manual Verification
- Verify `bitcoin-cli -testnet getnetworkinfo` (or similar) reports correct ports/magic if reachable.
