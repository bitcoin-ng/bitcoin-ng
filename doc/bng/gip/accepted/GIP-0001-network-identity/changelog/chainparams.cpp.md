
# `chainparams.cpp` changelog notes (GIP-0001)

This document summarizes the key changes made in `src/kernel/chainparams.cpp` as part of the “network identity” work for BNG.

## Summary of changes

- **New network identity / message starts**
	- Custom P2P message start bytes for the BNG networks:
		- Main: `BNG\x01`
		- Testnet: `BNG\x02`
		- Regtest: `BNG\x03`
	- Purpose: prevent cross-network/cross-fork connections and accidental interoperability with Bitcoin networks.

- **Ports**
	- Custom default ports to match BNG’s identity and avoid conflicts:
		- Main: `9333`
		- Testnet: `19333`
		- Regtest: `19444`

- **Address encodings**
	- New Base58 prefixes and bech32 human-readable parts (HRPs) for BNG.
	- Example HRPs seen in the change:
		- Main: `bng`
		- Regtest: `bngr`
	- Purpose: make addresses visually and programmatically distinct from Bitcoin.

- **Seeds**
	- DNS seeds were cleared/replaced with placeholders (e.g. `seed.bng.example.com.`).
	- Purpose: a new chain does not have established seed infrastructure; placeholders avoid implying production readiness.

- **Genesis blocks and assertions**
	- New genesis settings for each network (timestamp string, time/bits/nonce).
	- Updated asserts for `hashGenesisBlock` and `hashMerkleRoot` to match the new genesis blocks.
	- Purpose: new chain identity implies new genesis blocks; the asserts protect against accidental drift.

- **ChainTxData**
	- For BNG main/testnet, chain transaction metadata was reset (often to zeros) because there is no established history yet.
	- Purpose: these constants are used for sync progress estimation and sanity checks; they must reflect the actual chain.

- **AssumeUTXO table (`m_assumeutxo_data`)**
	- For BNG main/testnet, `m_assumeutxo_data` is intentionally **empty**.

## AssumeUTXO: what `m_assumeutxo_data` is

`m_assumeutxo_data` is a per-chain table of **AssumeUTXO snapshots** (precomputed UTXO set snapshots) keyed by height.

Each entry typically anchors a known-good snapshot using:

- `height`: block height where the snapshot is taken
- a snapshot hash (expected hash of the serialized UTXO snapshot)
- the chain transaction count at that height (used for checks / progress)
- the block hash at that height (anchors the snapshot to a specific chain)

This enables **fast initial sync**: a node can start from a trusted UTXO snapshot and then validate forward to tip.
See also: `doc/assumeutxo.md`.

## Why `m_assumeutxo_data` is empty for BNG

On established networks (Bitcoin mainnet/test networks), upstream Bitcoin Core ships with a curated set of AssumeUTXO snapshot entries.

For BNG, the chain identity (genesis + history) is new, so upstream snapshot metadata would be **incorrect**. Keeping stale entries would:

- fail snapshot validation immediately (best case), or
- mislead operators into thinking “fast sync” is supported/secure when it is not.

Therefore, the table is cleared for BNG main/testnet until BNG has its own stable, reproducible snapshots.

## Why some networks may still have entries

If `chainparams.cpp` still contains AssumeUTXO entries for other chains (e.g. signet/testnet4), those are only valid if those chains are still the upstream Bitcoin networks.

Regtest often keeps entries because test suites can rely on fixed snapshot metadata.

## How to add BNG AssumeUTXO entries later

Once the BNG chain is stable and you want snapshot-based sync:

1. Choose a snapshot height (commonly a round number, after consensus and deployment stability).
2. Sync a reference node to that height.
3. Produce a UTXO snapshot using the snapshot tooling/RPC available in this tree.
4. Record the snapshot’s expected hash, the block hash at the snapshot height, and the chain tx count.
5. Add a new entry in `m_assumeutxo_data` for that network.

