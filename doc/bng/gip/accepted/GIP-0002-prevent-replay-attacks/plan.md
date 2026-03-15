# BNG-0002 (GIP-0002): Implementation Plan — Replay Attack Protection

## How and why it works

### Problem recap (what we are preventing)
BNG forks Bitcoin’s UTXO set (same outpoints exist on both chains). If BNG uses *the same consensus signature-hash (sighash) algorithms* as Bitcoin, then a transaction signed for one chain may also be valid on the other chain.

Changing Base58 prefixes / Bech32 HRPs is **not** replay protection: it only changes human encoding of the same underlying `scriptPubKey`.

### Core mechanism: signature digest domain separation
BNG-0002 prevents “classic” cross-chain replay by ensuring that **BNG and Bitcoin compute different signature digests for the same transaction data**.

If the digest differs, the signature differs; and a signature created/validated on Bitcoin will fail verification on BNG (and vice versa).

Concretely:

1) **ECDSA (legacy + segwit v0)**

- In Bitcoin-derived code, both legacy (pre-segwit) and segwit v0 (BIP143) signature hashes commit to a 32-bit little-endian hashtype value appended to the preimage.
- The signature itself still appends a 1-byte sighash type at the end of the DER blob.

BNG-0002 uses that mismatch (32-bit commitment vs 1-byte encoding) to domain-separate without changing the script-visible sighash byte:

- Keep the *script-level* sighash byte unchanged (e.g. `SIGHASH_ALL`, `SIGHASH_ALL|ANYONECANPAY`).
- Change the **32-bit hashtype committed in the digest** from:

  - Bitcoin: `nHashType` (where the upper 24 bits are effectively 0)
  - BNG: `ForkedSighashType(nHashType)` (where the low byte is identical to `nHashType`, but the upper bytes commit to a BNG fork identifier)

This means:
- Bitcoin signatures (computed with upper 24 bits = 0) do not verify on BNG.
- BNG signatures do not verify on Bitcoin.

2) **Schnorr (taproot/tapscript, BIP341/BIP342)**

Taproot’s `TapSighash` digest is a tagged hash preimage that commits to the transaction data and a 1-byte `hash_type` (or `SIGHASH_DEFAULT` when omitted).

BNG-0002 domain-separates taproot by inserting a fixed fork identifier (`BNG_SIGHASH_FORKID`) into the `TapSighash` preimage, so the resulting tagged hash differs from Bitcoin’s for the same transaction.

### What this does and does not protect

**Protected (typical user spends):**
- P2PKH / bare checksig (legacy ECDSA)
- P2WPKH / P2WSH (segwit v0 ECDSA)
- P2TR key-path + script-path spends (taproot schnorr)

**Not protected (by design):**
- Anyone-can-spend outputs (e.g. `OP_TRUE`) and other spends that require no signature.

### Activation model
This is a **consensus-breaking** change relative to Bitcoin’s sighash rules.

- If BNG is a new chain (genesis chain) or a snapshot chain where BNG defines consensus from height 0/1 onward, replay protection can be **always-on**.
- If BNG is a live fork at height $H$, this must be gated behind an activation mechanism. The plan below assumes always-on at height 1 unless explicitly called out.

## How to test

(For build/test environment setup, use the repo’s shared instructions in doc/bng/development/README.md.)

### 1) Static audit (must pass before running tests)
Goal: ensure **every** consensus signature-hash path commits to the forkid.

Run:
- `git grep "\bSignatureHash\(" src/`
- `git grep "SignatureHashSchnorr" src/`

Expectation:
- Only the consensus implementations in `src/script/interpreter.cpp` are used.
- There are no alternative sighash implementations used for validation.

### 2) Unit tests (consensus-level coverage)

#### A) Sighash vectors must be updated
BNG-0002 changes the digest, so Bitcoin’s existing unit tests that assert “BNG == Bitcoin” will fail.

Specifically, once you implement replay protection:
- `src/test/sighash_tests.cpp`’s `sighash_test` currently checks `SignatureHash(...) == SignatureHashOld(...)` for BASE spends.
- `src/test/data/sighash.json.h` (and the generator that produced it) encodes Bitcoin sighash values.

Update test intent to one of these models (pick one and keep it consistent):

1) **BNG-only vectors (recommended for always-on replay protection)**
- Replace/update the JSON vectors to BNG values.
- Update/replace the “old hash” equivalence test so it *no longer asserts equality with Bitcoin’s legacy algorithm*.

2) **Dual-mode vectors (only if you implement height-gated activation)**
- Keep the old vectors for pre-activation heights.
- Add new BNG vectors for post-activation heights.

#### B) Add explicit replay protection tests
Add unit tests that prove “Bitcoin-style signature fails, BNG-style signature succeeds” for:
- Legacy ECDSA (SigVersion::BASE)
- Segwit v0 ECDSA (SigVersion::WITNESS_V0)
- Taproot schnorr (SigVersion::TAPROOT and SigVersion::TAPSCRIPT)

Practical way to do this in C++ tests:
- Implement a helper in the test file that computes the **Bitcoin** digest (i.e., old behavior) even after the code is changed.
  - For ECDSA BASE, you can reuse the existing `SignatureHashOld(...)` pattern from `src/test/sighash_tests.cpp`.
  - For segwit v0, implement a minimal “old” BIP143 preimage builder in the test (or factor out a reusable helper under `src/test/util/`).
  - For taproot, implement a minimal “old” preimage builder by copying the current `SignatureHashSchnorr` logic but *omitting the forkid field*.
- Sign both digests with the same key, then verify:
  - the Bitcoin-style signature fails when checked using the normal script interpreter path
  - the BNG-style signature succeeds

### 3) Functional / integration tests (end-to-end)

Add a functional test that:
1. Mines blocks to get spendable coins.
2. Creates a standard spend (P2WPKH is a good baseline).
3. Produces two candidate signatures for the same input:
   - one using the old (Bitcoin) digest
   - one using the new (BNG) digest
4. Sends the old-style transaction and asserts `sendrawtransaction` fails with a script/invalid-signature error.
5. Sends the new-style transaction and asserts it is accepted.

If you want to validate actual cross-chain replay (optional/manual):
- Construct a transaction on Bitcoin Core spending a “snapshot-matching” output.
- Try broadcasting the identical raw transaction to a BNG node.
- Expectation: BNG rejects with signature verification failure.

## Detailed implementation changes

### 0) Decide the forkid constant and its semantics
BNG-0002 assumes a 4-byte fork identifier that is consensus-critical.

- Choose a constant and document it as a 32-bit integer that is serialized little-endian inside signature-hash preimages.
- Example from the GIP README rationale: `0x00474E42` which serializes as bytes `42 4e 47 00` (ASCII `"BNG\0"`).

Define it exactly once in code (single source of truth).

### 1) Introduce consensus parameters (chainparams / consensus params)

**Files:**
- `src/consensus/params.h`
- `src/kernel/chainparams.cpp`
- (optional) `src/deploymentinfo.cpp` (only if you add height-gated activation via `-testactivationheight`)

**Changes:**

1. Add new consensus fields to `Consensus::Params`:
   - `uint32_t sighash_forkid;`
   - `int replay_protection_height;` (if you want gating; otherwise omit and treat as always-on)

2. In `src/kernel/chainparams.cpp`, set them for each network:
   - main/test/signet/regtest: `consensus.sighash_forkid = 0x00474E42;`
   - for snapshot/genesis behavior: `consensus.replay_protection_height = 1;` (or `0` if you want it active on genesis block evaluation too)

3. (Optional but recommended for test ergonomics) Add a new buried deployment name to control activation height on regtest:
   - Extend `Consensus::BuriedDeployment` in `src/consensus/params.h` with `DEPLOYMENT_REPLAY_PROTECTION`.
   - Add it to `Consensus::Params::DeploymentHeight(...)`.
   - Add name mapping in `src/deploymentinfo.cpp` / `GetBuriedDeployment`.
   - Extend the `-testactivationheight` switch handling in `src/kernel/chainparams.cpp`’s regtest override switch.

This lets you do: `-testactivationheight=replayprotection@200` (name exact to be defined) for boundary testing.

### 2) Implement ForkedSighashType() and use it in ECDSA digest paths

**Files:**
- `src/script/interpreter.cpp`
- `src/script/interpreter.h`

**Goal:** every ECDSA signature digest path must commit to `ForkedSighashType(nHashType)` instead of `nHashType`.

**Implementation steps:**

1. Add a small helper in `src/script/interpreter.cpp` (or a new header in `src/script/`) such as:
   - `static inline uint32_t ForkedSighashType(uint8_t sighash_byte, uint32_t forkid)`

   Definition requirement:
   - low 8 bits must equal the original `sighash_byte` (the byte appended to the DER signature)
   - upper 24 bits must commit to `forkid`

   One straightforward mapping is:
   - `forked = uint32_t(sighash_byte) | (forkid << 8)`

2. Modify `SignatureHash(...)` in `src/script/interpreter.cpp`:

   - Identify both branches:
     - segwit v0 (`sigversion == SigVersion::WITNESS_V0`)
     - legacy (`else` branch)

   - Ensure that the 32-bit value appended at the end of the preimage uses the forked type:
     - replace `ss << nHashType;` with `ss << ForkedSighashType(uint8_t(nHashType), forkid);`

   Notes:
   - `SignatureHash(...)` currently receives `int32_t nHashType` but it is derived from the last signature byte.
   - Preserve behavior for invalid `SIGHASH_SINGLE` (return `uint256::ONE`).

3. Update caching interactions:

   - The `SigHashCache` keying and midstate storage depends on `hash_type`.
   - Ensure you use the same “forked” 32-bit value consistently when:
     - looking up cache entries
     - storing cache entries
     - appending the hashtype to the preimage

4. Plumb forkid into call sites (choose one approach):

   **Approach A (explicit, preferred):**
   - Extend `SignatureHash(...)` signature to accept `uint32_t forkid` (with no default).
   - Update *all* call sites (notably in `src/script/sign.cpp`, tests, bench, fuzz) to pass `Params().GetConsensus().sighash_forkid` (or an injected value in tests).

   **Approach B (always-on constant, minimal plumbing):**
   - Keep function signature the same.
   - Use a compile-time constant forkid inside the function.
   - This is simpler but makes per-network forkid selection harder.

This plan assumes Approach A if you want the forkid to live in `chainparams`.

### 3) Domain-separate taproot/tapscript Schnorr sighashes

**Files:**
- `src/script/interpreter.cpp`
- `src/script/interpreter.h`

**Goal:** ensure `SignatureHashSchnorr(...)` commits to the forkid.

**Implementation steps:**

1. Add a 4-byte forkid commitment into the taproot sighash preimage, near the top (recommended placement):
   - Immediately after the epoch byte (`EPOCH`) and before `hash_type`.

2. Plumb forkid into `SignatureHashSchnorr(...)` similarly to ECDSA:
   - Either add `uint32_t forkid` parameter, or use a shared injected/global constant.

3. Update schnorr signing code path:
   - `src/script/sign.cpp` calls `SignatureHashSchnorr(...)` in `MutableTransactionSignatureCreator::CreateSchnorrSig`.
   - Ensure it passes the same forkid used by validation.

### 4) Activation (if you need a non-genesis fork)

If replay protection must activate at height $H$ (not always-on), you need both signing and validation to switch at the same boundary.

Key constraint: `SignatureHash(...)` and `SignatureHashSchnorr(...)` do not currently have access to the block height.

Two workable designs:

1) **Plumb activation through the signature checker / signature creator (recommended)**
- Add a boolean (or forkid value) to `GenericTransactionSignatureChecker` and `MutableTransactionSignatureCreator`.
- Set it at construction time based on the current block height / consensus params.
- Pass it into the hashing functions.

2) **Plumb activation via script flags (only if you’re willing to change APIs)**
- Add a new `SCRIPT_VERIFY_*` flag.
- Extend signature checker/hashing APIs to take flags.

In either case, the height decision point belongs in validation code that already knows height:
- `src/validation.cpp` (block connection and script checking paths)

### 5) Update and extend tests

**Files likely to change:**
- `src/test/sighash_tests.cpp`
- `src/test/data/sighash.json.h` (and its generator, if any)
- `src/test/script_tests.cpp` (if it asserts specific sighash values)
- `src/test/txvalidationcache_tests.cpp` (it computes hashes directly)
- `src/test/fuzz/script_interpreter.cpp`
- `src/bench/verify_script.cpp`

**Work items:**
1. Replace/adjust any tests that assert equality with Bitcoin’s legacy sighash algorithm.
2. Add targeted replay protection tests (fail old-style, succeed new-style) for BASE/WITNESS_V0/TAPROOT/TAPSCRIPT.
3. Regenerate sighash vector fixtures:
   - Use the existing `PRINT_SIGHASH_JSON` capability in `src/test/sighash_tests.cpp` as a starting point, but ensure vectors represent the *new* behavior.

### 6) Wallet/RPC behavior expectations

No new RPC parameters are required if you keep the 1-byte sighash type encoding unchanged.

However, update expectations:
- Any code/tooling that compares BNG sighashes against Bitcoin Core’s values must be updated.
- PSBT signing/verifying remains structurally compatible, but signatures created on Bitcoin will not verify on BNG.

If you introduce activation gating, you must ensure wallet signing switches at the same boundary (height or network rule) as validation.

---

## Implementation order (to avoid long debugging loops)

1. Add forkid constant + consensus/chainparams plumbing.
2. Update ECDSA `SignatureHash(...)` to commit to forkid.
3. Update Schnorr `SignatureHashSchnorr(...)` to commit to forkid.
4. Fix compilation by updating call sites (signing + tests + bench/fuzz).
5. Update unit tests and regenerate vectors.
6. Add replay-protection-specific unit and functional tests.

## Acceptance criteria

BNG-0002 is “done” when all of the following are true:

- Every ECDSA sighash path (legacy + segwit v0) commits to the forkid.
- Every taproot/tapscript sighash path commits to the forkid.
- Unit tests include at least one proof that a Bitcoin-style signature fails on BNG.
- Updated sighash vector tests pass.
- A functional test demonstrates old-style tx rejection and new-style tx acceptance.

---

## 2026-03-15 exact execution plan for this repo

This section resolves the remaining implementation choices against the current `bitcoin-ng` tree so the work can be executed in a straight line.

### Resolved choices for the first implementation pass

1. **Activation model:** treat replay protection as **always on** for BNG.
   - Do **not** add height-gated activation machinery in this pass.
   - Do **not** add a new buried deployment or `-testactivationheight` hook yet.
   - Rationale: the current repo already behaves as a distinct BNG chain on all shipped networks, and adding height plumbing would significantly widen the patch surface.

2. **Forkid source of truth:** use a single consensus constant for now.
   - Standardize the name to `BNG_REPLAY_PROTECTION_FORKID`.
   - Value: `0x00474E42`.
   - Serialize little-endian where committed into preimages.
   - Recommended placement: `src/script/interpreter.h` with definition in `src/script/interpreter.cpp` if needed.
   - Follow-up if needed later: move to `Consensus::Params` only if per-network configurability becomes necessary.

3. **ECDSA forked hashtype mapping:** use:
   - `ForkedSighashType(sighash_byte) = uint32_t(sighash_byte) | (BNG_REPLAY_PROTECTION_FORKID << 8)`
   - This preserves the low byte used in the DER-appended sighash marker while changing the 32-bit committed value.

4. **SigHashCache behavior:** keep the existing cache slot selection logic.
   - `SigHashCache::CacheIndex()` only depends on the low-bit sighash mode and `ANYONECANPAY`, which still come from the unchanged script-level sighash byte.
   - The production cache API does **not** need a new key type for the always-on constant approach.
   - Tests that manually reconstruct cached hashes **do** need to append the forked 32-bit value instead of the old raw `hash_type`.

### Exact code sequence

#### Step 1: introduce the replay-protection helpers

**Files:**
- `src/script/interpreter.h`
- `src/script/interpreter.cpp`

**Edits:**
- Add `BNG_REPLAY_PROTECTION_FORKID`.
- Add a small helper for the ECDSA path:
  - `static inline uint32_t ForkedSighashType(uint8_t sighash_byte)`
- Add a helper for the taproot path if it improves readability:
  - e.g. `static constexpr uint32_t BNG_REPLAY_PROTECTION_FORKID = ...`

**Definition rules:**
- Low 8 bits remain the original sighash byte.
- Upper 24 bits commit to the BNG forkid.
- With `0x00474E42`, the committed ECDSA hashtype bytes become `[sighash_byte, 0x42, 0x4e, 0x47]`.

#### Step 2: change ECDSA sighash construction

**Primary file:**
- `src/script/interpreter.cpp`

**Function:**
- `SignatureHash(...)`

**Edits:**
- Compute the forked 32-bit committed hashtype once near the top of the function, after the `SIGHASH_SINGLE` out-of-range check:
  - `const uint32_t forked_hash_type = ForkedSighashType(uint8_t(nHashType));`
- Replace the final `ss << nHashType;` in both cache-hit and cache-miss paths with:
  - `ss << forked_hash_type;`
- Leave all mode branching (`ALL/NONE/SINGLE`, `ANYONECANPAY`) driven by the original `nHashType`, because script-visible sighash semantics do not change.

**Important non-change:**
- Do **not** alter the 1-byte sighash appended to serialized signatures in signing code.

#### Step 3: change taproot/tapscript sighash construction

**Primary file:**
- `src/script/interpreter.cpp`

**Function:**
- `SignatureHashSchnorr(...)`

**Edits:**
- Insert the 4-byte little-endian `BNG_REPLAY_PROTECTION_FORKID` into the `TapSighash` preimage immediately after the epoch byte and before the 1-byte `hash_type`.
- Keep the existing hashtype validation rules unchanged:
  - `SIGHASH_DEFAULT`
  - `SIGHASH_ALL`
  - `SIGHASH_NONE`
  - `SIGHASH_SINGLE`
  - optional `SIGHASH_ANYONECANPAY`

**Result:**
- Key-path and script-path schnorr signatures become BNG-specific without changing the externally encoded 64/65-byte signature format.

#### Step 4: rebuild any direct sighash call sites that assume Bitcoin outputs

These call sites should compile unchanged if the helper stays internal to `interpreter.cpp`, but they must be reviewed because their expected values or comments may now be wrong.

**Signing paths to verify:**
- `src/script/sign.cpp`

**Direct hash users to review/update:**
- `src/bench/verify_script.cpp`
- `src/test/script_tests.cpp`
- `src/test/multisig_tests.cpp`
- `src/test/txvalidationcache_tests.cpp`
- `src/test/fuzz/script_interpreter.cpp`
- `src/test/sighash_tests.cpp`

**Expectation:**
- No caller should need to pass a new forkid argument in the first pass.
- The behavioral change comes entirely from the hashing functions.

#### Step 5: update unit tests to match BNG consensus

**Primary file:**
- `src/test/sighash_tests.cpp`

**Concrete edits:**
- Keep `SignatureHashOld(...)` as a Bitcoin-legacy helper for negative testing only.
- Remove or invert the assertion that BASE `SignatureHash(...) == SignatureHashOld(...)`.
- Add explicit checks that:
  - BASE BNG sighash differs from `SignatureHashOld(...)`.
  - repeated cache/no-cache computations still match each other.
- In the cache-manipulation portion of the test, update the manually appended hashtype from raw `hash_type` to the forked 32-bit committed value.

**Why this matters:**
- Today the cache test reconstructs a midstate and appends `hash_type` manually.
- After the replay-protection change, that manual reconstruction will silently encode the wrong digest unless it is updated.

#### Step 6: replace Bitcoin-based sighash vectors

**Files:**
- `src/test/data/sighash.json`
- `src/test/data/sighash.json.h`
- `src/test/sighash_tests.cpp`
- any local generator or fixture workflow used by `PRINT_SIGHASH_JSON`

**Concrete plan:**
- Regenerate vectors from the modified BNG `SignatureHash(...)` implementation.
- Keep the vector format unchanged if possible; only replace expected digest values.
- Ensure the committed values reflect the BNG forked ECDSA digest, not upstream Bitcoin values.

**Open check while doing this step:**
- Confirm whether `src/test/data/sighash.json.h` is generated in-tree or committed manually from `src/test/data/sighash.json`.

#### Step 7: add targeted replay-protection proofs

**Recommended file:**
- rather then `src/test/script_tests.cpp`, do add a new dedicated replay-protection test file under `src/test/bng/`

**Add at least these cases:**
- BASE: old Bitcoin-style ECDSA signature fails, BNG-style signature succeeds.
- WITNESS_V0: old Bitcoin-style BIP143 digest signature fails, BNG-style signature succeeds.
- TAPROOT key-path: old taproot preimage without forkid fails, BNG-style succeeds.
- TAPSCRIPT script-path: same proof for tapscript if practical in the same file.

**Implementation detail:**
- For segwit v0 and taproot, create test-local “old behavior” helpers instead of trying to keep production code dual-mode.

#### Step 8: add one end-to-end functional test

**Recommended location:**
- `test/functional/`

**Scenario:**
1. Mine spendable coins.
2. Build a standard witness spend, preferably P2WPKH for the first pass.
3. Produce one transaction signed with the old digest and one signed with the BNG digest.
4. Assert the old-style raw transaction is rejected.
5. Assert the BNG-style raw transaction is accepted.

**Why P2WPKH first:**
- It exercises replay protection in a common modern path without the extra control-block/script-path setup taproot tests need.

### Current open items after this plan update

These are the only meaningful items still open before coding starts:

- the replay-protection proof tests live in new `src/test/bng/replay_protection_tests.cpp`.
- Confirm the preferred fixture regeneration workflow for `sighash.json` / `sighash.json.h`.
- patch the accepted GIP `README.md` eventually to stop overstating current implementation status

### Recommended implementation order

Use this exact order to minimize rework:

1. Patch `src/script/interpreter.*` for ECDSA and schnorr digest separation.
2. Run/build unit tests that compile the direct sighash users.
3. Fix `src/test/sighash_tests.cpp` cache/equality assumptions.
4. Regenerate and commit updated sighash fixtures.
5. Add replay-protection proof tests.
6. Add the functional test last.
