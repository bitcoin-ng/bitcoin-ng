# BNG -> CTAM Branch-Managed Mining Integration

## Goal

Patch BNG mining so that BNG block proof-of-work is executed by the external
CTAM server over REST.

This plan intentionally chooses:

### Jump straight to explicit branch-managed mining

BNG is a Bitcoin Core fork. It must keep original Bitcoin Core behavior
unchanged except for the BNG block proof-of-work path.

BNG must not implement `cta_sha256d` or `csha256d` locally. Those functions are
provided by CTAM. CTAM is the only executor of BNG block proof-of-work.

This note is an implementation plan for the accepted GIP direction. It is not a
consensus specification.

## Terminology

- **BNG** is the Bitcoin NG client codebase. It is forked from Bitcoin Core and
  should preserve original Bitcoin Core behavior except for the BNG block
  proof-of-work path.
- **Client** means BNG. In this plan, "client" means the Bitcoin Core-derived
  node process that assembles, mines, submits, and validates blocks.
- **CTAM** means the external CTAM server. It is the context state management
  machine that executes the BNG proof-of-work function and exposes that work
  through REST APIs such as `/mining` and `/crypto/cta_sha256`.
- **Server** means CTAM. BNG calls this server over REST when it needs BNG block
  proof-of-work executed.
- **BNG block proof-of-work** means the PoW function used by BNG mining:
  `cta_sha256d` / `csha256d`.
- **REST client** means the BNG-side HTTP client code used to call CTAM. The
  BNG codebase must use this REST client rather than implementing
  `cta_sha256d` / `csha256d` locally.
- **CTAM-Backend** means the planned BNG-side C++ helper, for example
  `node::CtamMiningBackend`. It is not the CTAM server. It is the shared BNG
  component that owns CTAM mining REST calls, request/response mapping, polling,
  branch-job handling, and solved-header verification.
- **RPC code** means BNG's Bitcoin Core-derived RPC server implementation, such
  as `src/rpc/mining.cpp`. It handles user-facing RPC commands like mining or
  block-generation commands. It should call `node::CtamMiningBackend` instead
  of constructing CTAM REST requests itself.
- **Validation code** means BNG's block validation path, such as
  `src/validation.cpp`. It should call `node::CtamMiningBackend` to verify BNG
  block proof-of-work instead of constructing CTAM REST requests itself.

The main implementation idea is: patch the PoW logic in original BNG, the
Bitcoin Core-derived client, so that BNG calls the CTAM server REST API for
`/mining` to produce valid block PoW without changing any unrelated original
Bitcoin Core behavior. BNG mining uses `cta_sha256d` / `csha256d`, but the BNG
client codebase does not implement those functions locally. It delegates them
to CTAM through the REST client.

## Boundary

Only BNG block proof-of-work is delegated to CTAM.

Do not replace, wrap, or modify ordinary Bitcoin Core hashing.

Keep local and unchanged:

- txid hashing
- merkle hashing
- script hashing
- witness hashing
- wallet hashing
- networking hashes
- cache keys
- any non-PoW SHA256 or SHA256d usage

The correct boundary is stricter than "all hashing used near mining." CTAM is
only for preparing candidate block proof-of-work, searching nonces, managing the
candidate mining branch, and verifying a solved BNG block header's proof.

## Current Status

The current code already has useful CTAM plumbing:

- runtime options for CTAM-backed mining
- a REST client and CTAM proof-of-work wrapper
- header-to-work serialization for CTAM mining requests
- RPC block generation that solves block proof-of-work through CTAM
- block validation that rechecks block proof-of-work through CTAM
- REST smoke tests covering CTAM proof-of-work hashing, direct mining, header
  mining, and mining jobs

The gap is ownership and branch semantics.


## Direction

Implement branch-managed mining directly.

Do not land an intermediate architecture whose final state is "generic CTAM
REST calls moved somewhere else." The new backend should be designed around
branch-managed mining from the start, even if CTAM's `/mining` API needs small
extensions to expose the required branch result and commit behavior.

The implementation still has one narrow boundary:

- BNG owns block assembly, requested difficulty, solved-header verification, and
  the decision to accept a winning result.
- CTAM owns CTA-SHA256 execution, nonce search, mining-job branch state, branch
  cleanup, and any branch retention/commit mechanics exposed by `/mining`.
- RPC and validation code call the internal implementation( `node::CtamMiningBackend` - which calls CTAM REST client). They do not construct CTAM REST calls directly.

## CTAM API Usage

For nonce search, BNG must use the CTAM mining job API:

```text
POST /mining
GET /mining/{job_id}
POST /mining/{job_id}/stop
```

For solved-header verification, BNG must call:

```text
POST /crypto/cta_sha256
```

Verification must:

- rebuild the exact solved block header bytes
- send those solved header bytes to CTAM
- not search nonces
- not retain or commit branches
- use `keep_branch = false`

`POST /crypto/mine_csha256` must not be used by new BNG mining management code.
If it remains available, treat it as a legacy compatibility endpoint only.

## Required Mining Request Mapping

When mining a block, BNG sends CTAM:

- `work_bytes_hex`: serialized block header/work bytes
- `nonce_offset`: offset of the 4-byte nonce
- `nonce_start`
- `nonce_end`
- `energy_difficulty_bits`: Bitcoin compact target bits
- `major_ctam_difficulty_context_size`: expected CTAM context size
- `minor_ctam_difficulty_rounds`: CTA-SHA256 injection/round difficulty
- `keep_branch`: `false` unless the `/mining` job explicitly needs to retain a
  candidate branch for BNG-controlled winner acceptance

The serialized header work must be the exact BNG block header bytes used for
proof-of-work. For a standard Bitcoin-style block header, the nonce offset is
76 and the serialized header length is 80 bytes.

## Branch Rule

BNG should not manually emulate branch mining in a client-side nonce loop.

Use CTAM's `/mining` API as the owner of mining-job branch behavior. If explicit
winning-branch retention, result acknowledgement, or commit semantics are
missing, extend `/mining` instead of adding BNG-side choreography around lower
level branch endpoints.

Required branch behavior:

- each candidate mining job has CTAM-owned candidate branch state
- stopped, failed, stale, and exhausted jobs do not commit branches
- a winning branch is retained or committed only through explicit `/mining`
  semantics
- BNG accepts at most one winning branch for a solved block
- validation-only `/crypto/cta_sha256` calls use transient branch state and
  never commit

## Client-Side Implementation

Add a mining-scoped REST backend in BNG, for example:

```cpp
node::CtamMiningBackend
```

This backend owns:

- CTAM REST configuration
- request and response DTOs
- block-header work serialization
- nonce offset handling
- CTAM difficulty field construction
- mining job creation, polling, stopping, and winner acceptance
- proof verification through solved-header hashing

`src/rpc/mining.cpp` and `src/validation.cpp` must call this backend instead of
constructing CTAM REST calls directly.

Suggested files:

- `src/node/ctam_miner.h`
- `src/node/ctam_miner.cpp`

Add `node/ctam_miner.cpp` to the `bitcoin_node` source list in
`src/CMakeLists.txt` near the existing `node/miner.cpp` entry.

Suggested public surface:

```cpp
namespace node {

struct CtamMiningOptions {
    bool enabled{true};
    CTAMClientOptions client_options;
};

struct CtamNonceRange {
    uint32_t start{0};
    uint32_t end{0};
};

struct CtamSolvedHeader {
    uint32_t nonce{0};
    uint256 digest;
};

CtamMiningOptions GetCtamMiningOptions(const ArgsManager& args);
CTAMDifficultyTriple BuildCtamDifficulty(const CBlockHeader& header, uint32_t context_size);

class CtamMiningBackend
{
public:
    explicit CtamMiningBackend(const ArgsManager& args);

    bool Enabled() const;
    uint32_t GetLiveContextSize() const;
    CTAMDifficultyTriple BuildDifficulty(const CBlockHeader& header) const;
    CtamHeaderMiningWork BuildHeaderWork(const CBlockHeader& header) const;

    bool SolveHeader(ChainstateManager& chainman, CBlockHeader& header, uint64_t& max_tries) const;
    bool VerifyHeader(const CBlockHeader& header) const;

private:
    CtamMiningOptions m_options;
};

} // namespace node
```

The exact names can change, but the backend must be the only place where BNG
builds CTAM mining requests, difficulty tuples, and verification calls.

## Implementation Plan

### Step 1. Add The CTAM-Mining Backend

Create the backend and move CTAM-specific ownership into it.

The backend should absorb:

- `-ctammining`, `-ctamresturl`, and `-ctamtimeoutms` parsing (see `init.cpp`)
- CTAM client option construction
- CTAM readiness checks
- live context size lookup
- difficulty tuple construction
- block header work serialization
- `/mining` job request construction
- `/mining/{job_id}` polling
- `/mining/{job_id}/stop` cancellation
- `/crypto/cta_sha256` solved-header verification

Do not add a local `cta_sha256d` or `csha256d` implementation. Do not add a
standard SHA256d fallback inside the backend.

### Step 2. Implement Branch-Managed `/mining` Solving

Make `SolveHeader()` use `/mining` job control for nonce search.

The backend should:

1. build exact header work bytes from the candidate block header
2. set `nonce_offset`
3. derive `energy_difficulty_bits` from `nBits`
4. derive the major CTAM context size from live CTAM state or consensus rules
5. set the minor CTAM difficulty rounds
6. submit `POST /mining`
7. poll `GET /mining/{job_id}`
8. stop the job on cancellation, stale work, or caller exhaustion
9. validate returned nonce and digest metadata before mutating the block header
10. accept or commit only the winning job branch through `/mining` semantics

If CTAM's current `/mining` response cannot distinguish retained, discarded, and
committed candidate branches, extend the response before relying on it for BNG
consensus-facing mining.

Suggested response fields:

- `job_id`
- `candidate_branch_key`
- `nonce_start`
- `nonce_end`
- `attempts`
- `found`
- `winning_nonce`
- `digest`
- `difficulty`
- `branch_status`: `discarded`, `retained`, or `committed`

### Step 3. Switch RPC Generation To The Backend

Modify `src/rpc/mining.cpp`.

RPC generation should call the backend rather than constructing a CTAM client or
CTAM request directly.

Suggested replacement shape:

```cpp
static bool GenerateCtamBlock(ChainstateManager& chainman, const ArgsManager& args, CBlock& block, uint64_t& max_tries)
{
    node::CtamMiningBackend backend{args};
    return backend.SolveHeader(chainman, block, max_tries);
}
```

This keeps RPC responsible for RPC behavior and leaves CTAM mining behavior in
the mining backend.

### Step 4. Switch Validation To The Backend

Modify `src/validation.cpp`.

Validation should call the same backend for BNG block proof verification:

```cpp
bool CheckCtamProofOfWork(const CBlockHeader& block)
{
    try {
        return node::CtamMiningBackend{gArgs}.VerifyHeader(block);
    } catch (const std::exception& e) {
        LogWarning("CTAM proof-of-work validation failed: %s\n", e.what());
        return false;
    }
}
```

`VerifyHeader()` must rebuild the exact solved block header bytes and call
`POST /crypto/cta_sha256` with `keep_branch = false`. It must not call
`/mining`, search a nonce range, retain a branch, or commit a branch.

### Step 5. Add Backend-Focused Tests

Add backend tests for:

- CTAM option parsing
- CTAM difficulty mapping from a block header
- header work serialization length and nonce offset
- malformed `/mining` response handling
- stopped or exhausted jobs not committing a branch
- winning job branch retention or commit behavior
- solved-header verification using `/crypto/cta_sha256` with `keep_branch = false`

Integration tests that require a local CTAM daemon should stay gated behind the
same CTAM test pattern already used by existing CTAM REST client tests.

### Step 6. Add Resize And Stale-Work Handling

The backend should absorb CTAM resize and stale-work rules.

Behavior to enforce:

- mining requests must not silently resize CTAM
- before starting work, live CTAM context size must match the requested major
  difficulty
- outstanding work becomes stale when the active CTAM `context_size` changes
- stale candidate branches are never committed
- mining resumes only after CTAM reports the required context size

If the local CTAM test environment cannot resize cheaply, write the backend
logic so it is unit-testable with a fake CTAM client before adding full
integration coverage.

## Test Plan

Build:

```bash
cmake --build build_ctam_rest_test --target test_bitcoin -j4
```

Run existing CTAM REST tests with a local CTAM daemon:

```bash
./run-ctam-tests.sh
```

Run targeted miner tests:

```bash
build_ctam_rest_test/bin/test_bitcoin --run_test=miner_tests/CreateNewBlock_validity --catch_system_error=no --log_level=test_suite
```

Run backend tests once added:

```bash
build_ctam_rest_test/bin/test_bitcoin --run_test=ctam_miner_tests --catch_system_error=no --log_level=test_suite -- -ctam=1
```

Run a regtest RPC smoke test with CTAM running:

```bash
build_ctam_rest_test/bin/bitcoind -regtest -datadir=/tmp/bng-ctam-rpc -fallbackfee=0.0001 -ctammining=1 -ctamresturl=http://127.0.0.1:8080/ -daemon
build_ctam_rest_test/bin/bitcoin-cli -regtest -datadir=/tmp/bng-ctam-rpc createwallet ctamtest
ADDRESS=$(build_ctam_rest_test/bin/bitcoin-cli -regtest -datadir=/tmp/bng-ctam-rpc getnewaddress)
build_ctam_rest_test/bin/bitcoin-cli -regtest -datadir=/tmp/bng-ctam-rpc generatetoaddress 1 "$ADDRESS" 4096
build_ctam_rest_test/bin/bitcoin-cli -regtest -datadir=/tmp/bng-ctam-rpc stop
```

Expected results:

- BNG compiles
- mining still works through existing RPCs
- validation checks CTAM PoW through the same backend
- generation uses `/mining` job control for nonce search
- verification uses `/crypto/cta_sha256` for solved-header checking
- no global SHA256 or SHA256d behavior changes
- no local `cta_sha256d` or `csha256d` implementation is added to BNG
- CTAM REST is the only executor of BNG block PoW

## Suggested Commit Breakdown

Keep the implementation reviewable:

1. add `node::CtamMiningBackend` and wire it into the build
2. implement branch-managed `/mining` solving in the backend
3. route RPC CTAM generation through the backend
4. route CTAM validation through the backend
5. add backend-focused tests and update `run-ctam-tests.sh`
6. add resize and stale-work handling

Avoid mixing unrelated Bitcoin Core hashing changes into these commits.

## Success Criteria

The patch is successful when:

- BNG compiles
- mining still works through existing RPCs
- validation checks CTAM PoW through the same backend
- `/mining` owns nonce search and mining-job branch behavior
- solved-header verification uses `/crypto/cta_sha256` with `keep_branch = false`
- no global SHA256 or SHA256d behavior changes
- no local `cta_sha256d` or `csha256d` implementation is added to BNG
- CTAM REST is the only executor of BNG block PoW

## Bottom Line

Jump straight to explicit branch-managed mining, but keep the boundary narrow.

BNG should delegate only block proof-of-work to CTAM. CTAM should own
branch-managed nonce search through `/mining`. BNG should centralize all CTAM
mining behavior in `node::CtamMiningBackend`, while RPC and validation become
callers of that backend rather than independent REST clients.
