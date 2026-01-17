
# Bitcoin Cash replay protection via SIGHASH_FORKID (code-guided walk-through)

This document is a code-first guide to how **Bitcoin Cash Node (BCHN)** prevents replay attacks against the Bitcoin (BTC) chain using **SIGHASH_FORKID** (the “ForkID” replay-protection scheme).

It’s written as a *research/tutorial* rather than a spec: the goal is to help you trace the actual implementation in this repository and understand how it differs from Bitcoin Core.

## Threat model (what “replay” means)

If two chains share history and transaction format, a transaction created for chain A can sometimes also be valid on chain B.

After BCH split from BTC (UAHF), the main replay risk was:

- A user spends coins on BCH.
- The same raw transaction (same inputs/outputs/signatures) is also accepted on BTC.
- Funds get unintentionally spent on BTC as well.

To prevent this, BCH made post-fork transactions **cryptographically different** in a way that BTC would reject.

## High-level design

BCH replay protection is implemented by:

1. Introducing a new signature-hash flag bit: **`SIGHASH_FORKID` = `0x40`**.
2. Defining a **different signature hashing algorithm** when this bit is set *and* the script flags allow it.
3. Activating a new consensus rule at the UAHF height that **requires** signatures to use `SIGHASH_FORKID`.

This combination is the key:

- BCH **requires** forkid signatures after activation.
- BTC **does not** switch to BCH’s forkid hashing algorithm, so a BCH transaction signed with forkid will fail validation on BTC.

## Code map (where to look)

Core implementation points in this repo:

- Sighash flags/type wrapper:
	- [src/script/sighashtype.h](src/script/sighashtype.h)
- Script flags (feature activation toggles):
	- [src/script/script_flags.h](src/script/script_flags.h)
- Enforcing “must use forkid” / “illegal forkid” rules:
	- [src/script/sigencoding.cpp](src/script/sigencoding.cpp)
	- Error types/strings: [src/script/script_error.h](src/script/script_error.h), [src/script/script_error.cpp](src/script/script_error.cpp)
- Interpreter and signature hash algorithm switch:
	- [src/script/interpreter.cpp](src/script/interpreter.cpp)
- Consensus activation (UAHF height) and plumbing:
	- [src/consensus/activation.cpp](src/consensus/activation.cpp)
	- Script-flag selection during validation: [src/validation.cpp](src/validation.cpp)
- Wallet/RPC signing defaults + enforcement:
	- [src/rpc/rawtransaction.cpp](src/rpc/rawtransaction.cpp)
	- Low-level signing: [src/script/sign.cpp](src/script/sign.cpp)
- Policy defaults (mempool/standardness):
	- [src/policy/policy.h](src/policy/policy.h)

Tests worth reading:

- [src/test/sigencoding_tests.cpp](src/test/sigencoding_tests.cpp)
- [src/test/sighash_tests.cpp](src/test/sighash_tests.cpp)
- [src/test/script_tests.cpp](src/test/script_tests.cpp)
- [src/test/data/script_tests.json](src/test/data/script_tests.json)

## End-to-end call path (mental model)

When a node validates a transaction input signature in a block:

1. Validation chooses script flags for the current height.
2. The interpreter checks signature encoding (`CheckTransactionSignatureEncoding`).
3. The interpreter computes the sighash digest (`SignatureHash`) based on the sighash type and flags.
4. The signature is verified against that digest.

Replay protection “hooks into” steps (1), (2), and (3).

## Step-by-step: how replay protection is implemented

### Step 1 — Define SIGHASH_FORKID and a sighash type helper

In [src/script/sighashtype.h](src/script/sighashtype.h#L13-L20) BCH defines:

```cpp
enum {
		SIGHASH_ALL = 1,
		SIGHASH_NONE = 2,
		SIGHASH_SINGLE = 3,
		SIGHASH_UTXOS = 0x20,
		SIGHASH_FORKID = 0x40,
		SIGHASH_ANYONECANPAY = 0x80,
};
```

It also defines `SigHashType`, a small wrapper used throughout signing and verification to test for flags like `hasFork()`.

Why this matters: BCH needs a canonical way to detect “this signature claims to be forkid-protected” via the last byte of the signature (the sighash type byte).

### Step 2 — Add a script-flag gate: SCRIPT_ENABLE_SIGHASH_FORKID

In [src/script/script_flags.h](src/script/script_flags.h#L82-L86), BCH adds a consensus/verification flag:

```cpp
// Do we accept signature using SIGHASH_FORKID
SCRIPT_ENABLE_SIGHASH_FORKID = (1U << 16),
```

This is critical: the network can decide (by height/upgrade) whether forkid signatures are legal, required, etc.

### Step 3 — Enforce “illegal forkid” and “must use forkid” in signature encoding

Replay protection isn’t just “support forkid”; it is “**require forkid after activation**”.

That enforcement is implemented in [src/script/sigencoding.cpp](src/script/sigencoding.cpp#L203-L246) inside `CheckSighashEncoding`.

Key logic (abridged):

```cpp
const auto hashType = GetHashType(vchSig);
const bool usesFork = hashType.hasFork();
const bool forkEnabled = flags & SCRIPT_ENABLE_SIGHASH_FORKID;

if (!forkEnabled && usesFork) {
		return set_error(serror, ScriptError::ILLEGAL_FORKID);
}

if (forkEnabled && !usesFork) {
		return set_error(serror, ScriptError::MUST_USE_FORKID);
}
```

The corresponding error codes are defined in:

- [src/script/script_error.h](src/script/script_error.h#L74-L83)
- [src/script/script_error.cpp](src/script/script_error.cpp#L104-L113)

This is the consensus “lock-in” that makes a replayable legacy signature invalid on BCH after UAHF.

Important nuance:

- This check is gated by `SCRIPT_VERIFY_STRICTENC` in the same function.
- That means the activation code must enable `SCRIPT_VERIFY_STRICTENC` alongside `SCRIPT_ENABLE_SIGHASH_FORKID`.

### Step 4 — Switch SignatureHash algorithm when forkid is used

The heart of replay protection is that a forkid signature commits to a digest computed *differently* than BTC would compute.

In [src/script/interpreter.cpp](src/script/interpreter.cpp#L2471-L2570), BCH’s `SignatureHash(...)` does:

```cpp
if (sigHashType.hasFork() && (flags & SCRIPT_ENABLE_SIGHASH_FORKID)) {
		// ... forkid hashing algorithm ...
		return {ss.GetHash(), ss.GetNumBytesWritten()};
}

// Otherwise: legacy algorithm
ss << txTmp << sigHashType;
return {ss.GetHash(), ss.GetNumBytesWritten()};
```

Inside the forkid hashing path you can see the “BIP143-like” structure:

- pre-hashing of prevouts, sequences, outputs
- explicit inclusion of the **spent amount** `prevTxOut.nValue`
- explicit serialization of `sigHashType`

All of this is done under the forkid branch.

Why this prevents replay:

- A BCH wallet signs using this forkid algorithm.
- A BTC node validating the same transaction does **not** compute this forkid digest.
- The signature won’t verify on BTC.

### Step 5 — Handle scriptCode cleaning differently for pre-fork signatures

In classic Bitcoin-style sighash, the interpreter removes the signature from `scriptCode` before hashing.

BCH keeps that behavior for non-forkid signatures, but *skips it* for forkid-protected signatures.

See `CleanupScriptCode` in [src/script/interpreter.cpp](src/script/interpreter.cpp#L116-L130):

```cpp
// Drop the signature in scripts when SIGHASH_FORKID is not used.
if (!(flags & SCRIPT_ENABLE_SIGHASH_FORKID) || !GetHashType(vchSig).hasFork()) {
		return {FindAndDelete(res.scriptCode, CScript(vchSig)), std::move(res)};
}
```

And where it is used for checksig: [src/script/interpreter.cpp](src/script/interpreter.cpp#L1419-L1447).

Takeaway: the forkid path is treated as its own signing domain.

### Step 6 — Activate at UAHF height: turn the flags on

Consensus activation is based on a height field in chain params.

The “is UAHF enabled?” logic is in [src/consensus/activation.cpp](src/consensus/activation.cpp#L9-L24):

```cpp
static bool IsUAHFenabled(const Consensus::Params &params, int nHeight) {
		return nHeight >= params.uahfHeight;
}
```

The actual place where script verification flags are selected for the next block is:

- [src/validation.cpp](src/validation.cpp#L1491-L1535) `GetNextBlockScriptFlags`

Relevant portion:

```cpp
// If the UAHF is enabled, we start accepting replay protected txns
if (IsUAHFenabled(params, pindex)) {
		flags |= SCRIPT_VERIFY_STRICTENC;
		flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
}
```

That single `if` statement is the bridge between “a new rule exists” and “the network enforces it from this height onward”.

### Step 7 — Make wallet/RPC signing require forkid

Consensus prevents miners from including non-forkid signatures, but wallets/RPC should also default to producing correct transactions.

RPC: `signrawtransactionwithkey` defaults to `"ALL|FORKID"` and hard-rejects non-forkid sighash types.

See [src/rpc/rawtransaction.cpp](src/rpc/rawtransaction.cpp#L1205-L1235):

```cpp
SigHashType sigHashType = ParseSighashString(hashType);
if (!sigHashType.hasFork()) {
		throw JSONRPCError(RPC_INVALID_PARAMETER,
											 "Signature must use SIGHASH_FORKID");
}
```

Signing: signatures are computed by calling `SignatureHash(...)` and then appending the sighash type byte.

See [src/script/sign.cpp](src/script/sign.cpp#L13-L35):

```cpp
const auto & [hash, bytesHashed] = SignatureHash(scriptCode, context, sigHashType, nullptr, scriptFlags);
key.SignECDSA(hash, vchSig);
vchSig.push_back(uint8_t(sigHashType.getRawSigHashType()));
```

Policy: the node’s default mempool policy includes forkid enabled in mandatory flags.

See [src/policy/policy.h](src/policy/policy.h#L97-L114) where `MANDATORY_SCRIPT_VERIFY_FLAGS` includes `SCRIPT_ENABLE_SIGHASH_FORKID`.

## How this differs from Bitcoin Core (practical comparison)

This repo is BCHN, not Bitcoin Core; so for Bitcoin Core you’ll need to cross-check in a Bitcoin Core checkout.

That said, the behavioral differences are straightforward:

1. **Bitcoin Core does not have `SCRIPT_ENABLE_SIGHASH_FORKID`** and therefore cannot require forkid signatures.
2. **Bitcoin Core does not compute the BCH forkid signature hash algorithm** for non-segwit scripts.
	 - Bitcoin Core uses the legacy pre-segwit algorithm for legacy scripts, and (for BTC’s segwit spends) uses BIP143.
	 - BCH applies a BIP143-like hashing scheme to its forkid mode.
3. **BCH introduces “MUST_USE_FORKID” as a consensus rule after UAHF**.
	 - This is a key replay-protection “hard wall”: transactions that might otherwise be valid on both chains are rejected on BCH.

In other words:

- BCH replay protection is implemented as a **new consensus rule + new sighash domain**.
- Bitcoin Core continues to validate transactions using its existing sighash rules; it does not opt into BCH’s forkid domain.

### Bitcoin Core “where to look” checklist (for a rigorous diff)

If you have a Bitcoin Core checkout, the fastest way to produce a precise, file-by-file comparison is to search for these symbols/keywords and compare behavior:

1. **Signature hash computation**
	 - Search for: `SignatureHash(`
	 - Typical location: `src/script/interpreter.cpp` (and related helpers)
	 - Questions to answer:
		 - Where does it choose between legacy vs BIP143 (SegWit) hashing?
		 - Does it ever branch on a `FORKID` bit? (it should not)
		 - What fields are included in the digest (amount? prevouts? sequences?)

2. **Signature encoding policy**
	 - Search for: `CheckSignatureEncoding`, `CheckTransactionSignatureEncoding`, `SCRIPT_VERIFY_STRICTENC`
	 - Typical locations: `src/script/interpreter.cpp`, `src/script/script_error.h`, and/or `src/script/interpreter.h`
	 - Questions to answer:
		 - Are there any errors equivalent to `MUST_USE_FORKID` / `ILLEGAL_FORKID`? (there should not be)
		 - How does strict encoding treat unknown sighash bits?

3. **Activation mechanism**
	 - Search for: `GetBlockScriptFlags`, `MANDATORY_SCRIPT_VERIFY_FLAGS`, `STANDARD_SCRIPT_VERIFY_FLAGS`
	 - Typical locations: `src/validation.cpp`, `src/policy/policy.h`
	 - Questions to answer:
		 - How do flags change over time (softfork deployment vs hardfork height checks)?
		 - Is there any point where signatures become *required* to have a new bit? (generally: no)

4. **RPC signing defaults**
	 - Search for: `signrawtransactionwithkey`, `sighashtype`, `ParseSighashString`
	 - Typical location: `src/rpc/rawtransaction.cpp`
	 - Questions to answer:
		 - What is the default sighash type?
		 - Does the RPC ever reject a sighash type for missing a certain bit? (BCH does for forkid)

When you do this comparison, the key “replay protection” distinction to highlight in your write-up is:

- BCH: Post-UAHF, `SCRIPT_ENABLE_SIGHASH_FORKID` is enabled and **non-forkid signatures are rejected**.
- BTC: There is **no forkid bit**, and no post-fork rule that forces signatures into a separate signing domain.

### Recommendation: how you’d modify Bitcoin Core v28.3 (research/educational)

Bitcoin Core v28.3 is a much more modular codebase than “UAHF-era” Core, so the *idea* ports cleanly, but the work is spread across more layers (consensus flags, script interpreter, wallet/RPC, and tests).

This is a practical checklist for a “BCH-style forkid replay-protection” experiment on top of Core v28.3 (e.g. on a new chain or regtest fork). It is **not** something you can deploy on Bitcoin mainnet as a softfork.

1. **Add the ForkID sighash bit**
	- Add a `SIGHASH_FORKID`-like constant (`0x40`) alongside the existing sighash flags.
	- In v28.3, find the authoritative definition by searching for `SIGHASH_ALL`.
	- Make sure the type/parser that interprets the final “sighash byte” can expose `hasFork()` (or equivalent).

2. **Add a script verification flag to gate acceptance**
	- Add `SCRIPT_ENABLE_SIGHASH_FORKID` (or similar) next to existing `SCRIPT_VERIFY_*` flags.
	- In Core, find this enum by searching for `SCRIPT_VERIFY_P2SH`.
	- This flag must be part of the contextual flags used during block validation.

3. **Enforce `ILLEGAL_FORKID` / `MUST_USE_FORKID` at the encoding layer**
	- In Core, signature encoding is checked in the interpreter before actually verifying signatures.
	- Search for: `CheckSignatureEncoding`, `IsValidSignatureEncoding`, `SCRIPT_VERIFY_STRICTENC`.
	- Add two new failure modes:
	  - ForkID present but flag disabled → reject (illegal forkid).
	  - Flag enabled but ForkID absent → reject (missing forkid).
	- Add new `ScriptError` values and strings to match.

4. **Branch the sighash algorithm when forkid is used**
	- Locate the legacy signature hash function by searching for `SignatureHash(`.
	- Add a branch like:
	  - if `(sigHashType.hasFork() && (flags & SCRIPT_ENABLE_SIGHASH_FORKID))` → compute BCH-style forkid digest.
	  - else → existing digest (legacy / segwit / taproot as appropriate).
	- Recommendation: limit ForkID behavior to the legacy (non-segwit) signature version to avoid accidental interactions with SegWit/Taproot sighash domains.

5. **Activate via a hardfork mechanism (not versionbits)**
	- BCH uses a height check (UAHF). Bitcoin Core’s primary mainnet activation mechanism is softfork versionbits, but “require new sighash bit” is not softfork-compatible.
	- For a controlled experiment:
	  - Add a `forkid_activation_height` to chainparams (or re-use a custom deployment mechanism).
	  - Turn on your `SCRIPT_ENABLE_SIGHASH_FORKID` (and strict encoding) when height ≥ activation.
	- Search for flag construction in Core by looking for `GetBlockScriptFlags` / `GetNextBlockScriptFlags` / `GetScriptVerificationFlags` patterns.

6. **Update signing defaults (wallet + RPC)**
	- Ensure wallet signing selects `ALL|FORKID` (or a forkid-containing default) after activation.
	- Update RPC parsing/validation to accept `FORKID` in sighash strings and (optionally) reject missing ForkID post-activation.
	- Search for: `signrawtransactionwithkey`, `sighashtype`, `ParseSighashString`, `SignatureCreator`.

7. **Tests you should expect to update/add (v28.3)**
	- Unit tests:
	  - Script error/encoding tests that assert exact `ScriptError` results.
	  - Sighash “golden vector” tests (expected digests change for forkid signatures).
	- Functional tests:
	  - Any test that spends outputs using raw transactions and expects legacy signatures to be accepted.
	- Practical approach:
	  - First add tests for the new rejection conditions (`ILLEGAL_FORKID`, `MUST_USE_FORKID`).
	  - Then add at least one end-to-end test that builds a forkid transaction and verifies it is valid under the new flags.

If you want this section to become a true side-by-side porting guide, share the exact Bitcoin Core v28.3 source tree (or at least a list of the relevant file paths in your checkout), and I’ll pin the checklist above to concrete file names/symbols as they exist in that version.

## Suggested research workflow (how to validate your understanding)

1. Start at [src/validation.cpp](src/validation.cpp#L1491-L1535) and confirm `SCRIPT_ENABLE_SIGHASH_FORKID` is activated at UAHF.
2. Read [src/script/sigencoding.cpp](src/script/sigencoding.cpp#L203-L246) and verify the precise conditions for `MUST_USE_FORKID`.
3. Read [src/script/interpreter.cpp](src/script/interpreter.cpp#L2471-L2570) and list what fields are hashed in forkid mode.
4. Read [src/rpc/rawtransaction.cpp](src/rpc/rawtransaction.cpp#L1205-L1235) and confirm signing is constrained to forkid.
5. Use tests in [src/test/sigencoding_tests.cpp](src/test/sigencoding_tests.cpp) and [src/test/sighash_tests.cpp](src/test/sighash_tests.cpp) to see expected edge cases.

## Minimal “step-by-step tutorial” summary

If you were implementing this from scratch in a Bitcoin-like codebase, the steps would be:

1. Add a new sighash bit (`0x40`) and a helper to parse/test it.
2. Add a script verification flag to gate acceptance of that bit.
3. Add new script errors: `ILLEGAL_FORKID` and `MUST_USE_FORKID`.
4. Extend signature encoding checks to:
	 - reject forkid unless flag enabled
	 - reject non-forkid once flag enabled
5. Implement a new sighash algorithm branch that is selected by `(hasFork && flagEnabled)`.
6. Wire activation (height/upgrade) to turn on the flag and strict encoding.
7. Update signing and RPC defaults to produce forkid signatures only.
8. Add unit tests for:
	 - error codes
	 - signature hash correctness
	 - activation behavior

---

## Note: yes, porting this requires patching tests

If you implement ForkID replay protection in a Bitcoin Core-derived codebase, you generally must update/patch tests because you changed consensus behavior and introduced new rejection reasons.

What typically breaks:

- Script/signature encoding test vectors: post-activation, non-forkid signatures now fail (new expected errors).
- Sighash golden-vector tests: forkid signatures use a different digest, so expected hashes must be updated/added.
- Mempool/policy tests: “standard” transactions may now need forkid sighash types by default.
- RPC signing tests: defaults/allowed sighash strings change; missing ForkID may become an RPC error.

---