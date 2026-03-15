# Bitcoin Script fork-id change

This note explains **why** the script interpreter changed for GIP-0002, **how** it changed, and **what did not change**.

## Why the interpreter changed

BNG shares Bitcoin's script system and transaction format closely enough that, without an extra chain-specific commitment, a signature created on Bitcoin could also validate on BNG.

That is the core replay-attack problem.

Changing address prefixes, HRPs, ports, or magic bytes helps users and tooling distinguish networks, but those changes do **not** alter the signature digest checked by `CHECKSIG`, `CHECKMULTISIG`, or Taproot verification.

To stop cross-chain replay, BNG needs the interpreter to validate signatures against a **different signing domain**.

## What changed

BNG added a chain-specific fork identifier:

- `BNG_REPLAY_PROTECTION_FORKID = 0x00474E42`

The interpreter now commits to that identifier inside the signature hash:

- **Legacy / pre-segwit ECDSA**: `SignatureHash(...)`
- **Segwit v0 ECDSA**: `SignatureHash(...)`
- **Taproot / Tapscript Schnorr**: `SignatureHashSchnorr(...)`

In practice:

- Legacy and segwit-v0 sighashes now append `ForkedSighashType(hashtype)` instead of committing only to the raw 1-byte hashtype.
- Taproot/Tapscript sighashes now include the BNG fork id in the `TapSighash` preimage.

Result:

- a Bitcoin-style signature does **not** validate on BNG
- a BNG signature does validate on BNG

## What did not change

This was **not** a change to Bitcoin Script semantics in the broad sense.

The following were **not** changed:

- Script opcodes and stack behavior
- `CHECKSIG` / `CHECKMULTISIG` success rules
- `CHECKLOCKTIMEVERIFY` / `CHECKSEQUENCEVERIFY` semantics
- Witness program structure
- Taproot execution rules
- Transaction serialization format
- The meaning of sighash modes such as `ALL`, `NONE`, `SINGLE`, and `ANYONECANPAY`

Also important:

- the script still carries the usual **1-byte sighash flag**
- BNG only changes the **digest committed to during verification/signing**

So the change is best understood as **signature-domain separation**, not a rewrite of Script itself.

## Test impact

This change intentionally breaks imported upstream vectors that embed Bitcoin-style signatures.

To handle that cleanly:

- imported upstream JSON/script fixtures are exercised with a legacy compatibility checker in tests, so they keep covering upstream script behavior
- BNG-native replay-protection coverage is exercised separately with fork-id-aware signatures

Relevant tests:

- `src/test/transaction_tests.cpp`
- `src/test/script_tests.cpp`
- `src/test/sighash_tests.cpp`
- `src/test/bng/replay_protection_tests.cpp`

## Short version

BNG did **not** change Script into a different language.

BNG changed the **signature hash domain** used by the interpreter so that signatures are chain-specific and cannot be replayed from Bitcoin onto BNG.
