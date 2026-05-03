# Implementation Plan — BNG-0001 (Network Identity and Rebranding)

This plan turns the accepted GIP into concrete repository changes. It is written to be actionable: every step lists the exact files to touch and what to change.

## Goals

1. **Hard network isolation** from Bitcoin (and other forks) via distinct P2P magic bytes and distinct chain genesis.
2. **Human-level isolation** via distinct address encodings (Base58 + Bech32 HRPs) and default ports.
3. **Operational safety**: avoid accidental shared datadirs/config files with Bitcoin Core.
4. Keep the change set reviewable and testable.

## Current status (as implemented)

- `src/kernel/chainparams.cpp`:
  - **BNG mainnet**: magic bytes, ports, seeds, address prefixes/HRP, new genesis, and Bitcoin-specific chain state defaults (assumevalid/assumeutxo/chaintxdata) have been updated/reset.
  - **BNG testnet (v3)**: magic bytes, ports, seeds, and a BNG genesis are in place; Base58 prefixes and bech32 HRP are aligned to GIP-0001.
  - **BNG regtest**: magic bytes, ports, and bech32 HRP are updated; regtest keeps its AssumeUTXO entries for unit/functional tests.
- **Validation pending**: build and test suite execution for these changes has not been recorded in this plan yet.

## Non-goals (for this GIP)

- Renaming binaries (`bitcoind`, `bitcoin-cli`, `bitcoin-qt`) or package names.
- Changing consensus rules beyond what is required to create a new genesis block.
- Updating historical Bitcoin Core release notes in `doc/release-notes/*`.

## Decision record (parameters to finalize)

Before code changes, pick and freeze these constants:

### A. P2P magic bytes
- Mainnet: 4 bytes (must not match any existing network in the codebase).
- Testnet: 4 bytes.
- Regtest: 4 bytes.

**Selection method (recommended):**
- Generate 32 random bytes and take the first 4.
- Verify they don’t match existing magics by searching in `src/kernel/chainparams.cpp`.

### B. Address version bytes
Pick values that don’t collide with Bitcoin’s well-known encodings:
- Base58 P2PKH version byte
- Base58 P2SH version byte
- Base58 WIF (secret key) version byte
- BIP32 extended key versions (4 bytes each): `EXT_PUBLIC_KEY`, `EXT_SECRET_KEY`

**Notes:**
- Avoid Bitcoin mainnet values (0, 5, 128, xpub/xprv versions).
- Avoid Bitcoin testnet/regtest values (111, 196, 239, tpub/tprv versions).

### C. Bech32 HRPs
Per GIP draft:
- Mainnet HRP: `bng`
- Testnet HRP: `tbng`
- Regtest HRP: `bngr`

### D. Default ports
Pick unique ports:
- P2P mainnet (currently 8333)
- P2P testnet (currently 18333)
- (Recommended) RPC ports in `src/chainparamsbase.cpp` (currently 8332/18332/18443)

### E. Genesis blocks
At minimum, mainnet must have a new genesis block. Decide scope:

- **Option 1 (recommended):** new genesis for mainnet + testnet; keep regtest genesis unchanged to avoid churn in developer tooling.
- **Option 2 (strictest):** new genesis for mainnet + testnet + regtest.

For each new genesis you need:
- timestamp message string
- output script pubkey (or a deterministic placeholder)
- `nTime`, `nNonce`, `nBits`, `nVersion`, reward

## Step-by-step implementation

### 1) Update the GIP text with final constants

**Files to modify**
- doc/bng/gip/accepted/GIP-0001-network-identity/README.md

**Changes**
- Replace all **[TO BE DETERMINED]** placeholders with the finalized values for:
  - main/test/reg magic bytes
  - Base58 versions and BIPeck32 HRPs
  - default ports
  - genesis block parameters (hash, merkle root, and the chosen timestamp message)
- Add a short “Rationale” subsection documenting how values were chosen (randomness, collision checks, etc.).

### 2) Implement network magic bytes, ports, prefixes, HRPs, and genesis in chain params

**Files to modify**
- src/kernel/chainparams.cpp

**Changes**
- In `CMainParams`:
  - Set `pchMessageStart[...]` to the new mainnet magic.
  - Set `nDefaultPort` to the new mainnet P2P port.
  - Replace `base58Prefixes[...]` values with the new BNG Base58 versions.
  - Set `bech32_hrp = "bng"`.
  - Replace Bitcoin genesis creation call/values with a newly generated BNG genesis.
  - Replace `consensus.hashGenesisBlock` assertion and `genesis.hashMerkleRoot` assertion with new values.
  - Clear or replace Bitcoin DNS seeds (`vSeeds`) and fixed seeds (`vFixedSeeds`) to avoid contacting Bitcoin infrastructure.
  - Remove or reset `consensus.nMinimumChainWork`, `consensus.defaultAssumeValid`, `m_assumeutxo_data`, and `chainTxData` for a fresh chain (these are Bitcoin-specific). Keep them empty/zeroed until real BNG chain data exists.

- In `CTestNetParams`:
  - Update `pchMessageStart[...]`, `nDefaultPort`, Base58 prefixes, and `bech32_hrp = "tbng"`.
  - Decide whether testnet gets a new genesis (recommended) and if yes update genesis and asserts.
  - Remove Bitcoin testnet DNS seeds and fixed seeds.
  - Reset chainwork/assumevalid/assumeutxo/chainTxData.

- In `CRegTestParams`:
  - Update `pchMessageStart[...]` to a new regtest magic.
  - Update Base58 prefixes if you want regtest Base58 to match testnet-style or a dedicated regtest prefix.
  - Set `bech32_hrp = "bngr"`.
  - **If choosing Option 2** (new regtest genesis): update genesis and asserts; update any tests that depend on the regtest genesis hash.

**Notes**
- Leaving `CTestNet4Params` and `SigNetParams` unchanged is acceptable if BNG intentionally continues to support Bitcoin signet/testnet4 for interoperability. If BNG wants full rebrand everywhere, treat these like testnet/mainnet and update them too (bigger scope).

### 3) Update RPC ports and net-specific data directories

This is not explicitly in the GIP, but it is strongly recommended for safe isolation.

**Files to modify**
- src/chainparamsbase.cpp
- src/common/args.cpp

**Changes**
- In `src/chainparamsbase.cpp`:
  - Change default RPC ports for main/test/reg to new non-Bitcoin values.
  - Consider renaming the testnet datadir from `testnet3` to `testnet` (optional; only if you’re intentionally breaking Bitcoin Core compatibility).

- In `src/common/args.cpp`:
  - Change `BITCOIN_CONF_FILENAME` from `bitcoin.conf` to a BNG-specific name (e.g. `bng.conf` or `bitcoin-ng.conf`).
  - Change `GetDefaultDataDir()` to a BNG-specific default directory:
    - Unix-like: `~/.bng` or `~/.bitcoin-ng`
    - macOS: `~/Library/Application Support/BNG` (or `Bitcoin-NG`)
    - Windows: `.../BNG` (or `Bitcoin-NG`)

**Follow-up docs/tests impacted by this change**
- CLI help strings and man pages reference `bitcoin.conf` and default paths; see Step 6.
- The functional test harness hardcodes `bitcoin.conf` and sometimes `.bitcoin`; if you rename the config file and/or default datadir, update:
  - `test/functional/test_framework/test_node.py` (sets `self.bitcoinconf = .../"bitcoin.conf"`)
  - `test/functional/test_framework/util.py` (writes/reads `bitcoin.conf`, and uses `.bitcoin` in a default datadir path)
  - `test/functional/feature_config_args.py` (creates/edits `bitcoin.conf`, validates error strings containing `bitcoin.conf`, and iterates chain dir names including `testnet3`)

### 4) Update address encoding/decoding logic in the functional test framework

These changes keep the Python functional tests aligned with the new address formats.

**Files to modify**
- test/functional/test_framework/address.py
- test/functional/test_framework/messages.py
- test/functional/test_framework/segwit_addr.py
- test/functional/test_framework/script_util.py

**Changes**
- In `test_framework/messages.py`:
  - Update `MAGIC_BYTES[...]` (P2P message start) for BNG networks so the Python P2P harness stays in sync with `src/kernel/chainparams.cpp`.
  - Verify any other per-network constants used by the P2P harness remain consistent after genesis/port/prefix changes.

- In `test_framework/address.py`:
  - Replace `ADDRESS_BCRT1_*` constants to use `bngr1...` encodings.
  - Update `program_to_witness()` to encode with `"bng"` on main and `"bngr"` (or `"tbng"`) on test/reg depending on how the tests are run.
  - Update `bech32_to_bytes()` accepted HRPs list to include `bng`, `tbng`, `bngr`.
  - Update Base58 version bytes (currently hardcoded `0/111` and `5/196`) to match your chosen BNG versions.

- In `test_framework/segwit_addr.py` and `script_util.py`:
  - Update any hardcoded assumptions about HRPs or address prefixes.

### 5) Update C++ unit tests and test vectors that embed Bitcoin addresses

**Files to modify**
- src/test/data/key_io_valid.json
- src/test/data/key_io_invalid.json
- src/test/data/bip341_wallet_vectors.json
- src/test/descriptor_tests.cpp
- src/test/script_standard_tests.cpp
- src/wallet/test/util.h

**Changes**
- Replace embedded `bc1...`, `tb1...`, `bcrt1...` strings with BNG equivalents.
  - Important: you cannot just change the prefix; Bech32 checksum must be recomputed.
- Replace any embedded Base58 addresses or WIFs that depend on version bytes.
- Update descriptor tests if they embed xpub/xprv/tpub/tprv strings. If BNG changes BIP32 version bytes, these strings must be regenerated.

### 6) Update functional tests that contain hardcoded addresses and/or depend on address formats

**Files to modify**
- test/functional/wallet_hd.py
- test/functional/wallet_signer.py
- test/functional/rpc_generate.py
- test/functional/rpc_decodescript.py
- test/functional/rpc_validateaddress.py
- test/functional/rpc_invalid_address_message.py
- test/functional/rpc_deriveaddresses.py
- test/functional/wallet_labels.py
- test/functional/wallet_importdescriptors.py
- test/functional/interface_rpc.py
- test/functional/mocks/signer.py
- test/functional/p2p_seednode.py
- test/functional/wallet_basic.py
- test/functional/feature_config_args.py
- test/functional/test_framework/test_node.py
- test/functional/test_framework/util.py
- test/functional/data/rpc_decodescript.json
- test/functional/data/rpc_psbt.json
- test/functional/data/util/bitcoin-util-test.json
- test/functional/data/util/txcreate*.json
- test/functional/data/util/txcreatemultisig3.json
- test/functional/data/util/txcreateoutpubkey2.json
- test/functional/data/util/txcreatescript3.json

**Changes**
- Replace `bcrt1...` regtest addresses with `bngr1...` equivalents.
- Replace any `bc1...`/`tb1...` values used as negative test vectors if your validation code now treats them differently.
- Update expected error messages if they include the expected HRP (e.g., “expected bc, got …” becomes “expected bng, got …”).
- If Base58 version bytes change (P2PKH/P2SH/WIF), update any embedded Base58 strings in functional tests and fixtures:
  - `test/functional/data/util/bitcoin-util-test.json` contains many `1...`/`3...` address literals and address parsing expectations.
  - Many `test/functional/data/util/txcreate*.json` fixtures embed legacy Base58 addresses and `addr(...)` descriptors.
  - `test/functional/rpc_invalid_address_message.py`, `test/functional/rpc_generate.py`, and `test/functional/wallet_basic.py` include legacy Base58 examples (often as negative test vectors) that will need to be regenerated or replaced.
- If Step 3 renames `bitcoin.conf` or changes the default datadir, update the functional harness and expectations:
  - `test/functional/test_framework/test_node.py`
  - `test/functional/test_framework/util.py`
  - `test/functional/feature_config_args.py` (also covers chain subdir naming such as `testnet3`)

- If you change default P2P ports, avoid hardcoded `8333` arithmetic in tests:
  - `test/functional/p2p_seednode.py` currently uses `8333 + i`; replace with the functional framework port helpers (or derive from node state) so the test survives a port rebrand.

### 7) Update docs and man pages that mention default ports and config filenames

Only update current/active docs and man pages. Do not update historical release notes.

**Files to modify**
- doc/JSON-RPC-interface.md
- doc/REST-interface.md
- doc/man/bitcoin-cli.1
- doc/man/bitcoin-qt.1
- doc/man/bitcoind.1
- doc/bng/gip/accepted/GIP-0001-network-identity/README.md (already in Step 1)

**Additional test docs impacted**
- test/functional/data/README.md

**Changes**
- Replace references to default ports (8332/8333/18332/18333/18443/18444) with new BNG defaults.
- If Step 3 changes the config filename, update references to `bitcoin.conf`.
- If Step 3 changes the default datadir, update paths.

- In `test/functional/data/README.md`:
  - Update any example RPC ports, Base58 addresses, and/or extended keys if they assume Bitcoin defaults that BNG changes (ports, Base58 versions, BIP32 versions).

### 8) Add a reproducible genesis generation tool (recommended)

Having a deterministic script prevents “mystery genesis” parameters and helps reviewers reproduce hashes.

**Files to add**
- contrib/devtools/generate_genesis_bng.py

**Script responsibilities**
- Produce genesis block header + coinbase transaction based on provided inputs.
- Print:
  - genesis hash
  - merkle root
  - `nTime`, `nNonce`, `nBits`, `nVersion`
  - coinbase scriptSig and scriptPubKey details
- Optionally print C++ snippet for `src/kernel/chainparams.cpp`.

### 9) Validation checklist (build + tests)

**Commands (developer workflow)**
- Configure & build using the existing build system.
- Run unit tests.
- Run a focused subset of functional tests that cover address encoding/decoding and wallet basics.

**What to verify**
- Nodes refuse to connect to Bitcoin peers (mismatched message start).
- Wallet generates `bng1...` (main) and `tbng1...` (test) and `bngr1...` (regtest) addresses.
- `getnewaddress`, `validateaddress`, `decodescript`, and descriptor RPCs reflect the new encodings.
- Default ports and RPC ports are updated and documented.
- Data directory is isolated from Bitcoin Core.

## Rollout notes

- This change is consensus- and wallet-facing. Communicate it clearly:
  - “Old addresses are invalid on BNG”
  - “BNG will not use Bitcoin datadirs/config by default”
- If any production chain already exists, changing genesis is a hard fork + full restart; in that case, pause and revisit the GIP scope.
