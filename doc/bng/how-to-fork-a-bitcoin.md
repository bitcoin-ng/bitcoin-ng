How to fork a Bitcoin-like chain

This guide walks you through the minimal, practical steps to create your own chain from this codebase. It focuses on where to change things, what to watch out for, and how to build and sanity‑check your result.

References for building and contributing:
- Linux/macOS: see `../build-unix.md` and `../build-osx.md`
- Windows (MSVC): see `../build-windows.md` and `build_msvc/README.md`
- Developer notes and conventions: `../developer-notes.md`

Assumptions
- You’re creating a new network (mainnet + testnet + regtest) with your own branding and parameters.
- You’ll keep the code layout largely the same and only change targeted constants and metadata.

High‑level checklist
1) Branding and naming
2) Networking (magic bytes, ports, seeds)
3) Consensus parameters (subsidy, supply, timing, size/weight, deployments)
4) Genesis block creation
5) Checkpoints and seeds regeneration
6) Build, run, and basic verification

Customizations

To customize your blockchain, make the following changes. For each item, update all networks: mainnet, testnet, and regtest.

Branding

1) Rename the project
    - Project/name strings:
      - `configure.ac` (AC_INIT project name), package metadata, and version defines.
      - GUI/window titles under `src/qt/` and icons under `share/pixmaps/` and `share/qt/`.
      - Client name/user agent strings typically in `src/clientversion.cpp` and `src/init.cpp`.
      - For Windows builds, review `build_msvc/*.vcxproj` display names if you plan to ship MSVC solutions.
    - Replace occurrences of the old name in `README.md`, man pages under `doc/man/`, and scripts in `contrib/` where applicable.

2) Change address prefixes
    - Base58 prefixes are defined in chain parameters, typically in `src/chainparams.cpp` via `Base58Type` arrays:
      - PUBKEY_ADDRESS (P2PKH), SCRIPT_ADDRESS (P2SH), SECRET_KEY (WIF), and extended keys (xpub/xprv variants).
    - Bech32 human‑readable part (HRP) for segwit addresses is also set per network (e.g., `bc`, `tb`, `bcrt` → choose your own like `xy`, `txy`).
    - Ensure testnet/regtest prefixes are distinct from mainnet.

Networking

1) Message prefix bytes
    - Set unique 4‑byte message start (aka magic) values for each network in `src/chainparams.cpp`.
    - Choose values unlikely to appear in normal data and unique vs. other coins. Don’t reuse Bitcoin’s.

2) RPC and P2P ports
    - Pick unique default P2P and RPC ports for mainnet, testnet, and regtest and set them in `src/chainparams.cpp` and any CLI help text.
    - Verify they don’t collide with common services on your platforms.

3) Seeds
    - Add DNS seeds and fixed seeds:
      - DNS seeds table in `src/chainparams.cpp` (look for `vSeeds`).
      - Fixed seeds in `src/chainparamsseeds.h` (generated from `contrib/seeds/` tools).
    - Initially you can run a few public nodes and hardcode them; later regenerate `chainparamsseeds.h` once your network has peers.

Consensus Rules

1) Coin distribution and premine (optional)
    - If you want a premine, you typically encode it in the genesis block coinbase or at a defined early height with consensus‑enforced rules. This requires custom logic beyond Bitcoin’s default. Keep it simple unless you fully understand the implications.

2) Max supply
    - In Bitcoin, maximum supply emerges from the halving schedule. You don’t set a single “max supply” constant; you control it via the initial subsidy and halving interval. Document your target cap.

3) Block rewards and halving
    - Configure the halving interval in consensus parameters (commonly `consensus.nSubsidyHalvingInterval` in `src/chainparams.cpp` / `src/consensus/params.h`).
    - The subsidy calculation is performed in `GetBlockSubsidy(...)` (commonly in `src/validation.cpp` in Core‑style layouts). Adjust only if you intend to deviate from halving.

4) Block size/weight and sigops limits
    - Modern Core uses block weight rather than raw size. Relevant constants live in policy and consensus headers (e.g., `MAX_BLOCK_WEIGHT`, `MAX_STANDARD_TX_WEIGHT`, sigops limits).
    - Adjust carefully: larger blocks need bandwidth, relay, and mempool policy updates; re‑run tests.

5) Block time and difficulty retargeting
    - Target spacing and retarget timespan are set in consensus params (e.g., `consensus.nPowTargetSpacing`, `consensus.nPowTargetTimespan`).
    - Regtest usually disables retargeting for faster testing; keep that behavior.
    - If you change PoW algorithm or add DGW/ASERT/KGW, that’s a deeper change across validation and consensus code.

6) Activate BIPs / deployments
    - Set heights for permanent activations (e.g., BIP34/65/66, segwit, taproot) and any versionbits deployments in `src/chainparams.cpp`.
    - For new networks, you can activate features from height 0 or near 0, but document the choices.

7) Checkpoint data
    - Optional. Define `CCheckpointData` entries in `src/chainparams.cpp` once your network advances to reduce IBD risk from deep reorgs.

Genesis block

You must create new genesis blocks for mainnet, testnet, and regtest. Typical steps:
1) Choose parameters
    - A human‑readable `pszTimestamp` (e.g., a recent headline), a pubkey for the genesis coinbase output, `nTime` (UNIX), `nBits` (initial difficulty), and `nVersion`.
2) Implement a quick search
    - Add a temporary utility (or use an external script) that constructs the candidate genesis block and increments `nNonce` until the block hash satisfies the difficulty encoded by `nBits`.
    - Keep `nTime` constant for reproducibility; search over `nNonce` and, if needed, `nTime`.
3) Record results
    - Note the found `hashGenesisBlock`, `hashMerkleRoot`, `nNonce`, and `nTime`. Update these in your network params in `src/chainparams.cpp`.
4) Repeat
    - Do this for testnet and regtest with much lower difficulty (larger `nBits`) so that mining blocks for testing is fast.

Tip: After setting a new genesis, delete any old `chainstate/` and `blocks/` directories when testing locally.

Seeds and checkpoints refresh
1) Bring up a few stable public nodes on your chosen P2P port.
2) Use `contrib/seeds/` tooling to harvest peer addresses and regenerate `src/chainparamsseeds.h`.
3) Optionally add a checkpoint at a modest height once your chain is stable.

Standards

1) OP_RETURN data limit
    - The standard relay limit for OP_RETURN payloads is defined in script/policy headers (e.g., `MAX_OP_RETURN_RELAY`). Increase only if you understand the relay/mempool impact.

Build, run, and verify
1) Build
    - Follow platform docs: `../build-unix.md`, `../build-windows.md`, `../build-osx.md`. For deterministic builds, see `contrib/` and `depends/`.
2) Smoke test
    - Start a node on regtest, generate a few blocks, and ensure addresses use your new prefixes and HRP. Verify P2P/RPC ports and magic bytes with packet captures or debug logs.
3) Functional tests
    - Run unit and functional tests. Adjust or disable only when you have an equivalent replacement. Update any tests that assume Bitcoin’s specific values (prefixes, ports, halving interval, etc.).

Notes and pitfalls
- Change values consistently across all networks; mismatches are a common source of silent failures.
- When altering block weight or mempool policy, review related constants in `src/policy/` and `src/consensus/` and re‑run tests.
- Avoid reusing Bitcoin’s seeds, magic, or ports to prevent accidental cross‑network connections.
- Keep a changelog of consensus and policy changes for operators and exchanges.


=====

# IMPROVEMENTS

Here’s a focused review of your guide—what’s solid, what’s missing, and precise upgrades you can fold in so someone can fork a Bitcoin-like chain without stepping on the usual landmines.

# Quick verdict

Good structure and scope. The essentials are there (branding, networking, consensus, genesis, build/test). What’s missing are: (1) a few consensus knobs that new chains often forget; (2) wallet/interop details (prefixes/derivation paths); (3) deployment hygiene (seeds, security, reproducible builds, release process); and (4) modern features you may want to pre-activate (SegWit/Taproot/compact blocks/filters, transport v2).

Below are concrete improvements you can paste back into the doc.

---

## Add/clarify these items

### Branding & metadata

* **License & attribution**: If the codebase is MIT/BSD, keep and update copyright headers and `COPYING`. Mention this explicitly to avoid compliance drift.
* **BIP32/BIP44/SLIP-132**: If you change xpub/xprv versions, also decide your **SLIP-132** style prefixes for ypub/zpub (if you keep them) and document your **BIP44 coin type** (register a provisional value; don’t collide with Bitcoin’s 0 / testnet’s 1).

  * Include example derivation paths: `m/44'/<coin_type>'/0'/0/index`, `m/84'...` (bech32), `m/86'...` (taproot).
* **Bech32 vs Bech32m**: Note that P2TR (taproot) uses **bech32m**. If you enable taproot, your HRP applies to both encodings; call out that senders must use bech32 for segwit v0 and bech32m for segwit v1+.

### Networking

* **Protocol version & services**: Note where to set `PROTOCOL_VERSION`, service bits (e.g., `NODE_WITNESS`, `NODE_COMPACT_FILTERS`), and `MIN_PEER_PROTO_VERSION`. This avoids accidental interop with the upstream network.
* **Assorted p2p hygiene**:

  * Decide whether to enable **BIP152 compact blocks** from day one (recommended).
  * Consider **BIP324 v2 transport** if your base supports it; specify default enable/disable and flags.
  * UPnP/IPv6/Tor defaults: document what you expect for operators.

### Seeds & discovery

* **DNS seed ops & diversity**: Document who runs them and require at least two independent operators; enable **DNSSEC** if you can.
* **Bootstrap plan**: Until DNS seeds are live, list `-addnode` examples and warn users to clear old peers.

### Consensus parameters (more complete checklist)

* **powLimit & nBits**: Explicitly set **`powLimit`** per network and explain that `nBits` for genesis must be consistent with it.
* **Difficulty adjustment**: If you keep Bitcoin’s retargeting, state clearly MTP usage and what you’re doing on testnet (allow min-difficulty blocks?) and regtest (no retargeting). If you diverge (DGW/ASERT/KGW), say this touches validation code (headers and blocks) and tests.
* **nMinimumChainWork & defaultAssumeValid**: For brand-new networks set these to **0x00**/**null** initially; add instructions to update them after a stable height to speed up IBD and improve safety.
* **Policy limits**: Mention **dust relay fee** and **min relay tx fee** (affects UX/economics). If you change block/tx weight, review:

  * `MAX_BLOCK_WEIGHT`, `MAX_STANDARD_TX_WEIGHT`
  * sigops limits (`MAX_BLOCK_SIGOPS_COST`, per-tx sigops policy)
  * package relay/RBF (BIP125) behavior if upstream supports it.
* **Script flags & mandatory rules**: If you “bury” BIP34/65/66/segwit/taproot from height 0, be explicit whether they’re enforced by height or MTP and ensure script verification flags align.
* **SIGHASH / fork-ID**: If you stay Bitcoin-compatible, no fork-ID. If you copy BCH-style `SIGHASH_FORKID`, call out that it’s a consensus change and breaks compatibility.
* **Address standardness**: If you increase OP_RETURN size or change standardness, warn that light wallets and indexers may lag.

### Genesis creation (tighten the procedure)

* **Determinism**: Instruct readers to **fix `pszTimestamp`, coinbase pubkey/script, `nTime`, `nVersion`, `nBits`** and only search `nNonce` (and `nTime` as fallback). Commit the small genesis-finder source used to derive the values so others can reproduce the hash.
* **Coinbase script**: Remind to use `CMutableTransaction` with a coinbase scriptSig embedding the timestamp text; output script should be a standard P2PK or P2PKH to a key you control (or provably unspendable if no premine).
* **Sanity checks**: After finding the genesis:

  * Verify `hashMerkleRoot` matches the constructed coinbase.
  * Ensure `hashGenesisBlock` matches `nBits` target.
  * Store `nTime`, `nNonce`, `hashGenesisBlock`, `hashMerkleRoot` in all three networks’ params.
* **Initial difficulty**: Recommend **very low difficulty** for testnet/regtest (`nBits` near `powLimit`) to ease testing.

### Checkpoints / chain data

* **CheckpointData & chainTxData**: Add a note to set `chainTxData` (tx/sec estimates) and checkpoint tuples once the network has history. This improves IBD estimates and header sync UX.

### Wallet & ecosystem compatibility

* **Descriptor wallets**: If the codebase supports descriptors, specify default descriptors for legacy/segwit/taproot so imports/exports work.
* **BIP21 URI**: Update the scheme and HRP examples in docs and tests.
* **QR/GUI assets**: Confirm address QR versions accept longer HRPs.
* **SLIP-44 coin type**: Explicitly document (even if provisional) to avoid cross-wallet collisions.

### Testing

* **What to run**: Beyond “unit and functional tests,” list:

  * `src/test/*` + `test/functional` with environment variables needed (e.g., `BITCOIN_TESTS=1` equivalents).
  * Update fixtures that assert **prefixes, HRPs, ports, halving interval**, genesis hash, and **address decoding**.
* **Fuzz & sanitizers**: Encourage running ASan/UBSan/TSan targets if supported.
* **p2p/IBD tests**: Add a short recipe to spin up 3 nodes, mine blocks on regtest, and verify compact-block announcements and filter serving.

### Build, release, and ops

* **Reproducible builds**: Point to `depends/` + Gitian/Guix (whichever your base uses). Define your signing keys, and require **detached sigs** for releases.
* **CI**: Recommend setting up CI (Linux/macOS/Windows), running unit/functional tests and lint.
* **Versioning**: Decide on SemVer or Core-style; write it in `configure.ac` and release notes.
* **Fork safety**: Advise against reusing upstream **seeds, magic, ports** (you already do) and also avoid **data directories** collisions by changing the default datadir name (e.g., `~/.yourcoin/`).

### Security & governance

* **Vulnerability handling**: Add a security policy email/key and private disclosure process.
* **Feature governance**: Document how/when you’ll activate future deployments (heights vs BIP9/Speedy Trial), and your minimum miner/user thresholds.

---

## Tighten a few existing lines

* **Address prefixes**: Mention **checksum/version collisions**. Use ranges that don’t decode to valid Bitcoin or popular forks’ prefixes to reduce user error. Provide examples (e.g., mainnet P2PKH leading char not starting with `1`/`3` to avoid confusion).
* **Ports**: Suggest checking against IANA and common services; include a one-liner to test conflicts (e.g., `ss -lntp` on Linux).
* **Seeds**: Remind readers to **regenerate `chainparamsseeds.h`** after the first week/month and again before any exchange listing to capture wider peer diversity.
* **OP_RETURN**: If you raise the limit, note **mempool bloat** and indexer cost; suggest keeping policy conservative even if consensus allows more.

---

## Suggested “basic verification” steps (make explicit)

1. **Address sanity**: Generate legacy, P2WPKH, and P2TR (if enabled) addresses; round-trip decode/encode; confirm HRP and version bytes.
2. **P2P handshake**: Use `-debug=net` and verify magic bytes, protocol version, service bits, and user agent on connect.
3. **Mining loop** (regtest): `generatetoaddress` 150+ blocks, confirm halving schedule math at expected heights.
4. **Mempool policy**: Broadcast a standard tx, RBF it once, and confirm replacement if you keep BIP125.
5. **Compact blocks / filters**: Confirm a peer serves BIP152 and (if enabled) BIP157/158 with `getcfcheckpt` / filter queries.
6. **IBD from scratch**: Delete `blocks/` and `chainstate/`, sync from your seed nodes; time it, capture logs, and confirm no unexpected reorgs.

---

## Minor copy/structure edits

* Rename “Standards → OP_RETURN data limit” to **“Policy: relay/standardness”** and include dust + min relay fee.
* Under **Consensus Rules (1)** make it explicit: “Premine requires consensus and validation logic; test coverage is mandatory. A genesis coinbase output alone is not enough if you intend multiple outputs or vesting schedules.”
* Add a short **“Not changing”** callout: “Signature scheme (secp256k1), script VM, and transaction format remain unchanged unless you explicitly modify validation code.”

---

## Forward-looking options to consider (optional)

* **Taproot at height 0** with bech32m HRP; enables modern wallets and more efficient multisig.
* **Compact block filters (BIP157/158)** from day one for light client support.
* **Transport v2 (BIP324)** default-off but available; document how to enable for privacy.
* **AssumeUTXO** (if your base has it) roadmap for faster IBD.

---

### TL;DR changes you can paste in

* Add sections for: **powLimit**, **nMinimumChainWork/defaultAssumeValid**, **BIP152/BIP157**, **BIP32/BIP44/SLIP-132**, **bech32m**, **security policy**, **release signing**, and **CI/reproducible builds**.
* Expand **Genesis** to stress determinism & reproducibility and include explicit coinbase/script guidance.
* Flesh out **Testing** with concrete tasks and mention sanitizers/fuzz.
* Document **policy** (dust, min relay fee, OP_RETURN, RBF) not just consensus.

If you want, I can turn these into exact paragraphs and PR-ready diffs for your doc.
