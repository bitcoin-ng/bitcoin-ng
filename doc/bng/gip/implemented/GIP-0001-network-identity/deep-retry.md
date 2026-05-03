# Deep retry notes — test folder learnings from `prev-attempt.txt`

This document extracts *test-suite and test-framework* changes that appeared in prior implementation attempts (captured in `prev-attempt.txt`) and turns them into an actionable checklist for BNG-0001.

It focuses on items that are easy to miss when doing the network identity rebrand (magic bytes, HRPs, Base58 versions, ports, config/datadir isolation, genesis).

> Scope note: `prev-attempt.txt` contains a very large patch touching many tests for reasons unrelated to BNG-0001 (upstream sync, API refactors, docs cleanup, etc.). This file only captures the pieces that affect or can silently break the network-identity work.

---

## 1) The big missing piece: P2P magic bytes inside the Python P2P harness

### What `prev-attempt.txt` showed
The functional test P2P stack maintains its own mapping of network “message start” bytes in:
- `test/functional/test_framework/messages.py` (`MAGIC_BYTES` dict)

In the patch excerpt, `MAGIC_BYTES` was edited (in at least one revision) to include BNG-like values:
- `mainnet/testnet3/regtest` set to `BNG\x01/BNG\x02/BNG\x03` (bytes `0x42 0x4e 0x47 ...`)

But another later edit in the same patch snapshot resets those to Bitcoin’s defaults (`f9beb4d9`, `1c163f28`, `fabfb5da`). The key takeaway is *not the actual constants* (they are inconsistent in the patch), but the fact that the test framework expects these values to match whatever the node uses.

### Action item for BNG-0001
Add an explicit step to keep the Python P2P harness in sync with chainparams:
- Update `test/functional/test_framework/messages.py` so `MAGIC_BYTES["mainnet"/"testnet*"/"regtest"]` match the final BNG message starts.
- Verify where `MAGIC_BYTES` is consumed (typically `test_framework/p2p.py` and message serialization) and ensure no other per-network constants are duplicated elsewhere.

**Why this matters:**
If you only change `src/kernel/chainparams.cpp` and forget the Python `MAGIC_BYTES`, P2P-driven functional tests will fail in confusing ways (handshake, message parsing, or “got garbage” errors).

---

## 2) Bech32 HRP churn: avoid “prefix swap” hacks; use payload-preserving re-encoding

### What `prev-attempt.txt` showed
At one point a helper existed in:
- `test/functional/rpc_deriveaddresses.py`

It attempted to “rebrand” `bcrt1...` → `bngr1...` by:
1) decoding the address with `decode_segwit_address('bcrt', addr)`
2) re-encoding with `encode_segwit_address('bngr', ver, payload)`

This is the *correct general technique* (payload-preserving re-encoding) — much better than a pure string prefix substitution.

However, the patch later removed this helper and returned to raw `bcrt1...` constants, which likely happened because the node under test still produced Bitcoin regtest HRPs at that time.

### Action item for BNG-0001
When you switch to HRPs `bng/tbng/bngr`, you will need either:
- a one-time fixture rewrite (regenerate the bech32 strings and commit them), or
- dynamic generation of expected addresses in tests (preferred for a small number of canonical constants), or
- a temporary conversion helper used only to migrate bulk fixtures (not ideal long term).

**Concrete places to apply:**
- `test/functional/test_framework/address.py` (canonical regtest addresses and helpers)
- `test/functional/test_framework/segwit_addr.py` (if any HRP lists/assumptions exist)
- any functional tests and JSON fixtures embedding `bc1/tb1/bcrt1` strings.

---

## 3) Canonical “unspendable” regtest addresses: consider making them computed, not literals

### What `prev-attempt.txt` showed
A prior attempt briefly changed `test/functional/test_framework/address.py` to compute canonical addresses dynamically for BNG regtest:
- compute `ADDRESS_BCRT1_UNSPENDABLE` via `encode_segwit_address('bngr', ...)`
- compute the descriptor via `descsum_create(f"addr({addr})")`

Then the patch snapshot reverted to hardcoded literal `bcrt1...` strings and also reverted `program_to_witness()` to use Bitcoin HRPs.

### Action item for BNG-0001
For BNG, prefer dynamic generation for a small set of canonical constants:
- `ADDRESS_*_UNSPENDABLE`
- “OP_TRUE spendable” witness programs used by tests

This reduces future churn: if HRP changes again, you don’t need to recompute + rewrite strings across dozens of tests.

---

## 4) Hardcoded ports inside functional tests: eliminate 8333/18333 assumptions where practical

### What `prev-attempt.txt` showed
A new file `test/functional/p2p_seednode.py` was introduced (in that patch snapshot) and hardcoded:
- `port = 8333 + i`

Even if this test is not currently in your tree, it’s a representative example of the pattern: tests sometimes treat 8333 as a “harmless default” even when they *should* use a helper.

### Action item for BNG-0001
After choosing new default ports, do a follow-up sweep for:
- arithmetic around 8333 / 18333
- strings like `127.0.0.1:8333`

Prefer replacing with existing functional framework helpers (e.g. `p2p_port(node_index)` / `rpc_port(node_index)` patterns) so tests keep working even if ports shift again.

---

## 5) Testnet3 naming and test data: deleting `blockheader_testnet3.hex` is a warning sign

### What `prev-attempt.txt` showed
The patch deleted:
- `test/functional/data/blockheader_testnet3.hex`

And other hunks referenced:
- using testnet3 headers “until the first checkpoint”

This directly intersects with BNG-0001 because:
- you plan to generate a new genesis (mainnet at minimum)
- you plan to reset checkpoints / chainwork / assumevalid (recommended)
- you may rename the chain subdir (`testnet3` → `testnet`) as part of isolation

### Action item for BNG-0001
Expect to touch tests that:
- assume a specific early checkpoint exists
- embed headers/blocks from Bitcoin testnet3
- refer to the chain name `testnet3` directly

If BNG testnet gets a new genesis, any file of “early headers from testnet3” becomes invalid and must be regenerated from the new chain (or the test rewritten to use regtest-only data).

---

## 6) “build/test/…” pathing and CMake integration can affect how you update fixtures

### What `prev-attempt.txt` showed
The patch introduced/builds around running tests from the build tree:
- new `test/CMakeLists.txt` that generates `build/test/config.ini` from `test/config.ini.in`
- symlinks (or copies) `test/functional/**` into `build/test/functional/**`
- doc updates in `test/README.md` + `test/functional/README.md` encouraging running `build/test/functional/test_runner.py`

### Action item for BNG-0001
When you rename config filename or default datadir (Step 3 in plan.md):
- ensure the build-generated `config.ini` (and any code reading it) remains consistent
- ensure documentation for running tests matches the build layout you expect

Also, when doing bulk rewrites of fixtures, make sure you edit the *source* files in `test/functional/...`, not generated copies under `build/test/...`.

---

## 7) Test data docs embed Bitcoin ports and addresses (not just tests)

### What `prev-attempt.txt` showed
The patch added `test/functional/data/README.md` containing examples with:
- RPC port `8332`
- Base58 address `1NQpH6...`
- descriptor containing `xprv...`

### Action item for BNG-0001
If you change:
- default RPC port(s), or
- Base58 versions / BIP32 versions,

then **test documentation** can become misleading or flat-out wrong.

This is not consensus-critical, but it’s a source of confusion when developers regenerate chains/vectors.

---

## 8) Checklist to fold back into the main plan

Add the following explicit sub-items to the implementation workstream (even if you keep plan.md unchanged, treat these as required “deep retry” checks):

1. **P2P harness sync**: update `test/functional/test_framework/messages.py` `MAGIC_BYTES` to match final BNG values.
2. **Bech32 fixture strategy**:
   - choose “rewrite fixtures” vs “dynamic generation” for canonical constants
   - avoid prefix swap; use decode+re-encode
3. **Ports sweep**: search for `8333/18333/18444/8332/18332/18443` in `test/functional/**` and replace hardcoded occurrences with helpers when possible.
4. **testnet3 dependency sweep**:
   - search for `blockheader_testnet3` and `testnet3` usage in `test/functional/**`
   - decide if BNG keeps “testnet3” naming (compat) or renames to “testnet” (isolation)
5. **Build-tree test layout**:
   - confirm whether CMake is expected to generate `build/test/config.ini`
   - ensure any config renames propagate to functional harness + docs
6. **Non-test docs under test/**: update any examples embedding Bitcoin-specific ports/addresses/xprv if they’re meant to describe BNG behavior.

---

## 9) Quick “where to look next” (within the patch)

If you want to mine more BNG-0001-relevant changes from `prev-attempt.txt`, the highest-signal strings are:
- `MAGIC_BYTES` (Python P2P harness)
- `bcrt1`, `bc1`, `tb1` (bech32 fixtures)
- `xpub`, `tpub`, `xprv`, `tprv` (BIP32 versions embedded)
- `bitcoin.conf`, `.bitcoin`, `testnet3` (config/datadir/chain naming)
- `8333`, `18333`, `18444`, `8332`, `18332`, `18443` (ports)

