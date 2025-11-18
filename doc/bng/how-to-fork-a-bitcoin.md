How to fork a Bitcoin-like chain

LEGEND
This guide is now split into two macro tracks:
    (A) Cosmetic & parameter-only changes: branding, directory names, address version bytes, ports, HRPs, user agent string, non‑consensus policy tuning. These keep you on the original protocol rules (no new validation semantics).
    (B) Consensus/protocol deviations: anything that alters block/transaction validation, supply derivation, script rules, difficulty algorithm, non‑standard premine logic, novel address/script types.

Read (A) end‑to‑end first. Only touch items in (B) if you intentionally want a chain that is not a drop‑in Bitcoin consensus clone.

This guide walks you through the minimal, practical steps to create your own chain from this codebase. It focuses on where to change things, what to watch out for, and how to build and sanity‑check your result.

References for building and contributing:
- Linux/macOS: see `../build-unix.md` and `../build-osx.md`
- Windows (MSVC): see `../build-windows.md` and `build_msvc/README.md`
- Developer notes and conventions: `../developer-notes.md`

Assumptions
- You’re creating a new network (mainnet + testnet + regtest) with your own branding and parameters.
- You’ll keep the code layout largely the same and only change targeted constants and metadata.

High‑level checklist
Section A (cosmetic/parameter):
    1) Branding and naming
    2) Networking identifiers (magic bytes, ports, seeds) – identifiers only, not protocol rules
    3) Address & key prefixes (Base58, Bech32 HRP)
    4) Data directory & application metadata
    5) Genesis block search (without changing validation semantics)
    6) Smoke tests & verification
Section B (consensus/protocol):
    7) Supply & subsidy model modifications
    8) Difficulty algorithm changes
    9) Script/activation rule adjustments (burying or redefining BIPs)
 10) Premine / special distribution logic
 11) Non‑standard block/transaction limits
 12) New deployments & feature governance

CUSTOMIZATIONS

SECTION (A): Cosmetic & Parameter‑Only Changes
These modifications do not introduce new consensus rules; they only change identifiers, metadata, or parameters accepted by existing logic. Apply consistently across mainnet, testnet, regtest (and signet/testnet4 if retained).

Branding & Metadata (A)

1) Rename the project
    - Project/name strings:
      - `configure.ac` (AC_INIT project name), package metadata, and version defines.
      - GUI/window titles under `src/qt/` and icons under `share/pixmaps/` and `share/qt/`.
      - Client name/user agent strings typically in `src/clientversion.cpp` and `src/init.cpp`.
      - For Windows builds, review `build_msvc/*.vcxproj` display names if you plan to ship MSVC solutions.
    - Replace occurrences of the old name in `README.md`, man pages under `doc/man/`, and scripts in `contrib/` where applicable.

2) Change address prefixes (A)
    - Base58 prefixes are defined in chain parameters, typically in `src/chainparams.cpp` via `Base58Type` arrays:
      - PUBKEY_ADDRESS (P2PKH), SCRIPT_ADDRESS (P2SH), SECRET_KEY (WIF), and extended keys (xpub/xprv variants).
    - Bech32 human‑readable part (HRP) for segwit addresses is also set per network (e.g., `bc`, `tb`, `bcrt` → choose your own like `xy`, `txy`).
    - Ensure testnet/regtest prefixes are distinct from mainnet.

Concrete rebranding recipe (BTC → BNG) (A)

The steps below are a copy‑paste checklist to create a new network named “BNG” with your own address prefixes and HRPs. Paths and symbols match this repository’s layout.

1) Set address version bytes and Bech32 HRPs
     - Edit `src/kernel/chainparams.cpp` in each chain class: `CMainParams`, `CTestNetParams`, `CTestNet4Params` (if you keep it), `SigNetParams` (optional), and `CRegTestParams`.
     - Change Base58 prefixes (P2PKH/P2SH/WIF/Ext keys):
         - Mainnet example:
             - `base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 38);`   // pick a value not used by popular chains
             - `base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 23);`
             - `base58Prefixes[SECRET_KEY]   = std::vector<unsigned char>(1, 168);`
             - `base58Prefixes[EXT_PUBLIC_KEY] = {0x04, 0xB2, 0x47, 0x46};`  // choose non‑Bitcoin versions if you want distinct xpub/xprv
             - `base58Prefixes[EXT_SECRET_KEY] = {0x04, 0xB2, 0x43, 0x0C};`
         - Testnet/regtest: pick distinct values, e.g., 111/196/239 are Bitcoin’s; choose different ones to avoid confusion.
     - Set Bech32 HRPs:
         - Mainnet: `bech32_hrp = "bng";`
         - Testnet: `bech32_hrp = "tbng";` (follows the “t…” convention)
         - Regtest: `bech32_hrp = "bngr";`
     - Notes:
         - Don’t aim for a specific first Base58 character; it’s not guaranteed. The goal is to avoid Bitcoin’s version bytes so users can’t confuse networks.
         - If you keep Bitcoin’s BIP32 versions (xpub/xprv), wallet export strings will still start with `xpub`/`xprv`. To fully rebrand, pick your own version bytes as above and verify interop with your wallets.

2) Choose unique magic bytes and default ports
     - In `src/kernel/chainparams.cpp`, update for each network:
         - `pchMessageStart[...]` 4 bytes (don’t reuse Bitcoin’s).
         - `nDefaultPort` P2P port (e.g., mainnet: 28xxx; testnet: 38xxx; regtest: 48xxx).
     - Update any help text/docs that mention ports.

3) Rename currency unit to BNG in the GUI
     - Edit `src/qt/bitcoinunits.cpp`:
         - Change names/descriptions: `BTC → BNG`, `mBTC → mBNG`, `uBTC → uBNG`. Keep “Satoshi (sat)” unless you deliberately rename satoshis.
     - This affects labels, formatting, and tooltips in the Qt wallet.

4) Switch Bech32 URI scheme to `bng:`
     - Edit these Qt files to replace `bitcoin:` with `bng:`:
         - `src/qt/guiutil.cpp` (functions `parseBitcoinURI`, `formatBitcoinURI`, comments/strings)
         - `src/qt/bitcoin.cpp` and `src/qt/bitcoingui.cpp` (status tips and comments)
         - `src/qt/test/uritests.cpp` and `src/qt/test/wallettests.cpp` (update test vectors)
     - Also update `QString("Enter a Bitcoin address…")` placeholders in `src/qt/guiutil.cpp` to say “BNG address”.

5) Change default data directory and app names
     - Default data dir: edit `src/common/args.cpp` → function `GetDefaultDataDir()`:
         - Windows: `…/Bitcoin` → `…/BNG`
         - macOS: `~/Library/Application Support/Bitcoin` → `…/BNG`
         - Linux/Unix: `~/.bitcoin` → `~/.bng`
     - Qt app names/org:
         - Edit `src/qt/guiconstants.h`:
             - `QAPP_ORG_NAME "Bitcoin"` → `"BNG"`
             - `QAPP_ORG_DOMAIN "bitcoin.org"` → your domain
             - `QAPP_APP_NAME_*` strings from `Bitcoin-Qt*` → `BNG-Qt*`
         - Icons: replace/adjust assets in `src/qt/bitcoin.qrc` and `share/pixmaps/` as needed.

6) Update user agent brand
     - Edit `src/clientversion.cpp`:
         - `const std::string CLIENT_NAME("Satoshi");` → set to your brand, e.g., `"BNG"` or `"BNGCore"`.
     - This changes the `/Brand:major.minor.patch/` user agent advertised on P2P.

7) Package metadata and docs
     - `configure.ac`: update `AC_INIT`, `PACKAGE_NAME`, `PACKAGE_URL`.
     - Docs: `README.md`, man pages under `doc/man/`, and any references in `contrib/` scripts.

8) Optional but recommended
     - Change the default data directory name at runtime (mentioned above) AND ensure any example paths in docs/tests reflect `~/.bng`.
     - Review and update BIP21 examples in `doc/` to use `bng:` and your HRPs.

9) Quick smoke tests (regtest)
     - Build and run a node on regtest, then verify branding and prefixes:
         - Generate an address of each type: legacy (if enabled), P2WPKH, and P2TR (if taproot is active). Check HRP begins with `bngr1…` on regtest.
         - Use the GUI to confirm units show BNG/mBNG/uBNG/sat and that payment request URIs start with `bng:`.
         - Confirm datadir printed in logs uses `.bng` and that the user agent contains your `CLIENT_NAME`.

10) Grep checklist to catch stragglers
     - Search and update occurrences:
         - `bitcoin:` → `bng:` (Qt/UI/tests)
         - `bc`/`tb`/`bcrt` HRPs → `bng`/`tbng`/`bngr` (chain params, tests, docs)
         - `Bitcoin`/`BTC` → `BNG` where appropriate (UI strings, docs; avoid changing copyright notices)
         - `.bitcoin` paths → `.bng`
         - DNS seeds: replace any `*.bitcoin.*` with your own once you have seeds

Why these exact edits?
- They touch all code locations that define address encodings (Base58 + Bech32), URIs, persistent paths, GUI strings, and network identity. Doing only a subset often leads to confusing UX or accidental cross‑network behavior.

"Not changing" (A)
- Signature scheme (secp256k1), transaction format, script VM, witness structure remain intact under pure cosmetic/parameter changes.
- The halving schedule and subsidy calculation logic remain identical unless you enter Section (B).
- No new opcodes or signature hash flags are introduced.


Networking Identifiers (A)

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

SECTION (B): Consensus & Protocol Deviations
Only proceed if you intend to diverge from Bitcoin consensus. Each item below can create incompatibility with upstream tooling and requires targeted test coverage.

Consensus & Supply (B)

1) Coin distribution & premine (B)
    - Warning: Merely editing the genesis coinbase text or adding outputs does NOT grant a secure, spendable premine under Bitcoin’s existing rules; the original genesis output is unspendable. A functional premine (multiple outputs, vesting, timelocks, staged release) requires explicit consensus logic: special validation paths at defined heights or scripts that must be enforced network‑wide. Introduce tests for: block acceptance at premine heights, correct spendability timing, rejection of unauthorized spends, chain reorg behavior. If you do not need a premine, skip entirely.

2) Max supply (B)
    - In Bitcoin, maximum supply emerges from the halving schedule. You don’t set a single “max supply” constant; you control it via the initial subsidy and halving interval. Document your target cap.

3) Block rewards & halving (B)
    - Configure the halving interval in consensus parameters (commonly `consensus.nSubsidyHalvingInterval` in `src/chainparams.cpp` / `src/consensus/params.h`).
    - The subsidy calculation is performed in `GetBlockSubsidy(...)` (commonly in `src/validation.cpp` in Core‑style layouts). Adjust only if you intend to deviate from halving.

4) Block size/weight & sigops limits (B)
    - Modern Core uses block weight rather than raw size. Relevant constants live in policy and consensus headers (e.g., `MAX_BLOCK_WEIGHT`, `MAX_STANDARD_TX_WEIGHT`, sigops limits).
    - Adjust carefully: larger blocks need bandwidth, relay, and mempool policy updates; re‑run tests.

5) Block time & difficulty retargeting (B)
    - Target spacing and retarget timespan are set in consensus params (e.g., `consensus.nPowTargetSpacing`, `consensus.nPowTargetTimespan`).
    - Regtest usually disables retargeting for faster testing; keep that behavior.
    - If you change PoW algorithm or add DGW/ASERT/KGW, that’s a deeper change across validation and consensus code.

6) Activate BIPs / deployments (B)
    - Set heights for permanent activations (e.g., BIP34/65/66, segwit, taproot) and any versionbits deployments in `src/chainparams.cpp`.
    - For new networks, you can activate features from height 0 or near 0, but document the choices.

7) Checkpoint data (B)
    - Optional. Define `CCheckpointData` entries in `src/chainparams.cpp` once your network advances to reduce IBD risk from deep reorgs.

Genesis Block (A for simple rebrand, B if altering validation)
If you ONLY search for a new hash with unchanged parameters (timestamp, nonce, nBits consistent with powLimit) you remain in track (A). If you modify difficulty rules, timestamp handling, or include custom premine validation tied to genesis, that crosses into (B).

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

Seeds & Checkpoints Refresh (A)
1) Bring up a few stable public nodes on your chosen P2P port.
2) Use `contrib/seeds/` tooling to harvest peer addresses and regenerate `src/chainparamsseeds.h`.
3) Optionally add a checkpoint at a modest height once your chain is stable.

Policy: relay & standardness (A)

1) OP_RETURN data limit
    - The standard relay limit for OP_RETURN payloads is defined in script/policy headers (e.g., `MAX_OP_RETURN_RELAY`). Increase only if you understand the relay/mempool impact.

Build, Run & Verify (A)
1) Build
    - Follow platform docs: `../build-unix.md`, `../build-windows.md`, `../build-osx.md`. For deterministic builds, see `contrib/` and `depends/`.
2) Smoke test
    - Start a node on regtest, generate a few blocks, and ensure addresses use your new prefixes and HRP. Verify P2P/RPC ports and magic bytes with packet captures or debug logs.
3) Functional tests
    - Run unit and functional tests. Adjust or disable only when you have an equivalent replacement. Update any tests that assume Bitcoin’s specific values (prefixes, ports, halving interval, etc.).

Notes & Pitfalls (A vs B)
For each change, ask: Does it alter validation? If yes → (B). Otherwise → (A).
- Change values consistently across all networks; mismatches are a common source of silent failures.
- When altering block weight or mempool policy, review related constants in `src/policy/` and `src/consensus/` and re‑run tests.
- Avoid reusing Bitcoin’s seeds, magic, or ports to prevent accidental cross‑network connections.
- Keep a changelog of consensus and policy changes for operators and exchanges.


APPENDIX: Additional Topics & Advanced Options (B)
The following were formerly in the improvements section; they are retained here, classified.

### Branding & metadata (A unless changing key/version formats)

* **License & attribution**: If the codebase is MIT/BSD, keep and update copyright headers and `COPYING`. Mention this explicitly to avoid compliance drift.
* **BIP32/BIP44/SLIP-132**: If you change xpub/xprv versions, also decide your **SLIP-132** style prefixes for ypub/zpub (if you keep them) and document your **BIP44 coin type** (register a provisional value; don’t collide with Bitcoin’s 0 / testnet’s 1).

  * Include example derivation paths: `m/44'/<coin_type>'/0'/0/index`, `m/84'...` (bech32), `m/86'...` (taproot).
* **Bech32 vs Bech32m**: Note that P2TR (taproot) uses **bech32m**. If you enable taproot, your HRP applies to both encodings; call out that senders must use bech32 for segwit v0 and bech32m for segwit v1+.

### Networking (A for identifiers, B if altering protocol versioning/service semantics)

* **Protocol version & services**: Note where to set `PROTOCOL_VERSION`, service bits (e.g., `NODE_WITNESS`, `NODE_COMPACT_FILTERS`), and `MIN_PEER_PROTO_VERSION`. This avoids accidental interop with the upstream network.
* **Assorted p2p hygiene**:

  * Decide whether to enable **BIP152 compact blocks** from day one (recommended).
  * Consider **BIP324 v2 transport** if your base supports it; specify default enable/disable and flags.
  * UPnP/IPv6/Tor defaults: document what you expect for operators.

### Seeds & discovery (A)

* **DNS seed ops & diversity**: Document who runs them and require at least two independent operators; enable **DNSSEC** if you can.
* **Bootstrap plan**: Until DNS seeds are live, list `-addnode` examples and warn users to clear old peers.

### Consensus parameters (B summary)

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

### Genesis creation (A baseline, B if validation semantics change)

* **Determinism**: Instruct readers to **fix `pszTimestamp`, coinbase pubkey/script, `nTime`, `nVersion`, `nBits`** and only search `nNonce` (and `nTime` as fallback). Commit the small genesis-finder source used to derive the values so others can reproduce the hash.
* **Coinbase script**: Remind to use `CMutableTransaction` with a coinbase scriptSig embedding the timestamp text; output script should be a standard P2PK or P2PKH to a key you control (or provably unspendable if no premine).
* **Sanity checks**: After finding the genesis:

  * Verify `hashMerkleRoot` matches the constructed coinbase.
  * Ensure `hashGenesisBlock` matches `nBits` target.
  * Store `nTime`, `nNonce`, `hashGenesisBlock`, `hashMerkleRoot` in all three networks’ params.
* **Initial difficulty**: Recommend **very low difficulty** for testnet/regtest (`nBits` near `powLimit`) to ease testing.

### Checkpoints / chain data (A, except if used to bypass validation which is discouraged)

* **CheckpointData & chainTxData**: Add a note to set `chainTxData` (tx/sec estimates) and checkpoint tuples once the network has history. This improves IBD estimates and header sync UX.

### Wallet & ecosystem compatibility (A unless adding new script/address types → B)

* **Descriptor wallets**: If the codebase supports descriptors, specify default descriptors for legacy/segwit/taproot so imports/exports work.
* **BIP21 URI**: Update the scheme and HRP examples in docs and tests.
* **QR/GUI assets**: Confirm address QR versions accept longer HRPs.
* **SLIP-44 coin type**: Explicitly document (even if provisional) to avoid cross-wallet collisions.

### Testing (A + B: extend tests when consensus changes occur)

* **What to run**: Beyond “unit and functional tests,” list:

  * `src/test/*` + `test/functional` with environment variables needed (e.g., `BITCOIN_TESTS=1` equivalents).
  * Update fixtures that assert **prefixes, HRPs, ports, halving interval**, genesis hash, and **address decoding**.
* **Fuzz & sanitizers**: Encourage running ASan/UBSan/TSan targets if supported.
* **p2p/IBD tests**: Add a short recipe to spin up 3 nodes, mine blocks on regtest, and verify compact-block announcements and filter serving.

### Build, release & ops (A)

* **Reproducible builds**: Point to `depends/` + Gitian/Guix (whichever your base uses). Define your signing keys, and require **detached sigs** for releases.
* **CI**: Recommend setting up CI (Linux/macOS/Windows), running unit/functional tests and lint.
* **Versioning**: Decide on SemVer or Core-style; write it in `configure.ac` and release notes.
* **Fork safety**: Advise against reusing upstream **seeds, magic, ports** (you already do) and also avoid **data directories** collisions by changing the default datadir name (e.g., `~/.yourcoin/`).

### Security & governance (A for policy documents; deployments scheduling logic touches B)

* **Vulnerability handling**: Add a security policy email/key and private disclosure process.
* **Feature governance**: Document how/when you’ll activate future deployments (heights vs BIP9/Speedy Trial), and your minimum miner/user thresholds.

---

## Tighten a few existing lines (Classify per item)

* **Address prefixes**: Mention **checksum/version collisions**. Use ranges that don’t decode to valid Bitcoin or popular forks’ prefixes to reduce user error. Provide examples (e.g., mainnet P2PKH leading char not starting with `1`/`3` to avoid confusion).
* **Ports**: Suggest checking against IANA and common services; include a one-liner to test conflicts (e.g., `ss -lntp` on Linux).
* **Seeds**: Remind readers to **regenerate `chainparamsseeds.h`** after the first week/month and again before any exchange listing to capture wider peer diversity.
* **OP_RETURN**: If you raise the limit, note **mempool bloat** and indexer cost; suggest keeping policy conservative even if consensus allows more.

---

## Suggested basic verification steps (A core; add consensus tests for B changes)

1. **Address sanity**: Generate legacy, P2WPKH, and P2TR (if enabled) addresses; round-trip decode/encode; confirm HRP and version bytes.
2. **P2P handshake**: Use `-debug=net` and verify magic bytes, protocol version, service bits, and user agent on connect.
3. **Mining loop** (regtest): `generatetoaddress` 150+ blocks, confirm halving schedule math at expected heights.
4. **Mempool policy**: Broadcast a standard tx, RBF it once, and confirm replacement if you keep BIP125.
5. **Compact blocks / filters**: Confirm a peer serves BIP152 and (if enabled) BIP157/158 with `getcfcheckpt` / filter queries.
6. **IBD from scratch**: Delete `blocks/` and `chainstate/`, sync from your seed nodes; time it, capture logs, and confirm no unexpected reorgs.

---

## Minor copy/structure edits (Applied in reorganization)

* Rename “Standards → OP_RETURN data limit” to **“Policy: relay/standardness”** and include dust + min relay fee.
* Under **Consensus Rules (1)** make it explicit: “Premine requires consensus and validation logic; test coverage is mandatory. A genesis coinbase output alone is not enough if you intend multiple outputs or vesting schedules.”
* Add a short **“Not changing”** callout: “Signature scheme (secp256k1), script VM, and transaction format remain unchanged unless you explicitly modify validation code.”

---

## Forward-looking options (Most are B)

* **Taproot at height 0** with bech32m HRP; enables modern wallets and more efficient multisig.
* **Compact block filters (BIP157/158)** from day one for light client support.
* **Transport v2 (BIP324)** default-off but available; document how to enable for privacy.
* **AssumeUTXO** (if your base has it) roadmap for faster IBD.

---

### TL;DR classification summary
Track (A): Branding, prefixes, HRPs, ports, seeds list, data directory, user agent string, genesis search (hash only), policy relay limits documentation.
Track (B): Premine logic, subsidy/halving alterations, difficulty algorithm changes, new activation heights logic if different semantics, block weight limit changes (consensus side), script flag modifications, new address/script encodings, feature governance that alters validation, novel deployments beyond simple burying.

Implementation reminder:
- When unsure, keep changes in (A) until you have test coverage and a clear rationale to move into (B).

If you want, I can turn these into exact paragraphs and PR-ready diffs for your doc.
