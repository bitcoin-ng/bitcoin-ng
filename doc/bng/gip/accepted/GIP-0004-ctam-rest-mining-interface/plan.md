# GIP-0004 Implementation Plan

## Goal
Implement the accepted CTAM REST mining direction without globally replacing Bitcoin Core's SHA-256 implementation. BNG should stay the client, CTAM should stay the external worker, and only the successfully mined CTAM branch may be committed.

## Constraints From The Current Code
- `src/crypto/sha256.h` defines `CSHA256`, which is used far beyond mining. A global replacement would leak CTAM behavior into txids, script hashing, wallet code, networking, and caches.
- `src/node/miner.cpp` is the correct ownership boundary for the first mining integration. It assembles candidate blocks and owns the PoW-facing control flow.
- `ctam/tests/blockchain/cxor_chain.py` demonstrates the intended separation: only the PoW hash path changes, while transaction ids, signatures, and UTXO logic remain standard.
- `ctam/doc/api/06-crypto.md` already exposes enough surface for a first client implementation:
  - `GET /health`
  - `GET /status`
  - `POST /branches`
  - `POST /branches/{key}/cxor`
  - `POST /branches/{key}/commit`
  - `POST /crypto/cta_sha256`
- CTAM commit semantics are strict: committing one branch promotes it to trunk and clears all other branches. The BNG client must therefore commit exactly one winning branch and leave losing branches uncommitted.

## Proposed Architecture

### 1. Add a mining-scoped CTAM client
Create a new client layer in BNG instead of changing `CSHA256` in place.

Suggested files:
- `src/crypto/ctam_client.h`
- `src/crypto/ctam_client.cpp`
- `src/crypto/ctam_sha256.h`
- `src/crypto/ctam_sha256.cpp`

Responsibilities:
- own CTAM REST configuration and HTTP transport
- expose a narrow API for health checks, branch lifecycle, CXOR operations, and mock CTA-SHA256 requests
- translate CTAM JSON failures into explicit BNG errors
- keep branch-management decisions inside BNG rather than relying on CTAM auto-cleanup as hidden control flow

The first implementation can use a simple blocking HTTP client. The important boundary is that mining code depends on a CTAM-specific interface, not on raw REST calls scattered through `miner.cpp`.

### 2. Introduce a mock external `csha256` path
Add a mining-only wrapper that behaves like a SHA-256 provider from the caller's point of view, but delegates hashing to CTAM over REST.

Use `POST /crypto/cta_sha256` for the first mock implementation because it already supports:
- single-block hashing
- configurable injection counts
- optional branch cloning
- custom initial state for chaining

This wrapper should be treated as a development and parity tool, not as a drop-in replacement for every `CSHA256` call site. It is appropriate for:
- mining-lane experiments
- parity tests against the Python reference in `ctam/tests/blockchain`
- benchmarks added next to `src/bench/crypto_hash.cpp`

It is not appropriate for:
- txid hashing
- witness/script hashing
- wallet/address derivation
- generic hash utility code in `src/hash.h`

### 3. Keep standard SHA-256 everywhere except the CTAM mining lane
Wire the new client into the memory-hard lane only.

Integration points:
- candidate block assembly remains in `src/node/miner.cpp`
- lane selection stays aligned with the dual-lane mining design in GIP-0003
- standard Bitcoin SHA256d remains the energy-lane path
- CTAM-backed hashing becomes the memory-lane path

The first pass should therefore add a mining strategy split instead of rewriting `CSHA256` globally.

### 4. Model branch lifecycle explicitly in BNG
BNG should own the branch workflow described by the CTAM blockchain prototype:

1. create a CTAM branch for a mining candidate
2. replay the required CXOR state transitions on that branch
3. evaluate the candidate hash or proof against the active target
4. commit the branch only after the candidate is accepted as the winning result
5. never commit losing branches

For the first prototype, this can be implemented with current CTAM endpoints:
- `POST /branches` to allocate a candidate branch
- `POST /branches/{key}/cxor` to mutate branch state during the CTA-SHA256 schedule
- `POST /branches/{key}/commit` only for the winning branch

This is safer than depending on `POST /crypto/cta_sha256` auto-created branches for the main mining loop, because BNG keeps control over which branch is promoted.

### 5. Preserve verification symmetry
BNG also needs a verifier-facing CTAM path for the memory-hard lane. The verifier should:
- reconstruct the same CTAM work inputs from block data
- recompute the CTAM-backed digest or proof through the same client abstraction
- reject malformed or inconsistent CTAM results as invalid data

The verification implementation can reuse the same CTAM client interface, but it must remain deterministic and consensus-scoped.

## Concrete Implementation Phases

### Phase 1. Configuration and plumbing
- add CTAM-specific runtime options such as:
  - `-ctamrest=1`
  - `-ctamresturl=http://127.0.0.1:8080/`
  - `-ctaminjections=<n>`
  - `-ctamtimeoutms=<n>`
- instantiate the CTAM client from node startup or mining context
- add a startup probe using `GET /health`

### Phase 2. Mock hash client
- implement the REST-backed mock `csha256` wrapper using `POST /crypto/cta_sha256`
- add parity tests against known responses
- add a benchmark entry alongside the existing SHA-256 benchmarks in `src/bench/crypto_hash.cpp`

### Phase 3. Mining-lane integration
- add a CTAM mining strategy in `src/node/miner.cpp`
- keep standard SHA256d untouched for non-CTAM lanes
- drive branch creation, CXOR mutation, and winning-branch commit from BNG code
- ensure branch commit happens only after local acceptance of the candidate

### Phase 4. Validation and failure handling
- reject CTAM responses with malformed hex, wrong digest size, or inconsistent branch identifiers
- fail cleanly when CTAM is unavailable
- ensure CTAM failures do not corrupt the standard mining path

### Phase 5. Developer workflow
- document the local CTAM companion setup
- add a Docker Compose or sidecar workflow later, but keep the first implementation local-only by default

## Test Plan

### Python contract test
Add a BNG-side Python test that starts the CTAM server, waits on `GET /health`, and drives the branch lifecycle entirely through BNG-owned client code. The test should prove:
- BNG can reach the CTAM REST service
- the mock CTA-SHA256 endpoint is callable as an external hash provider
- multiple candidate branches can be created and mutated independently
- only the winning branch is committed
- after commit, CTAM reports the committed branch as the new trunk key and all other branches are gone

### C++ tests to add after the Python contract is in place
- unit tests for CTAM client request/response parsing
- mining tests for lane selection and CTAM failure fallback
- verifier tests for malformed CTAM responses

## Gaps Still Open
- `POST /crypto/cta_sha256` is currently single-block and optimized for parity and prototype work, not a final high-throughput mining API.
- If mining throughput or branch reuse becomes a bottleneck, CTAM should grow a dedicated mining endpoint later instead of overloading generic crypto routes.
- The final consensus encoding for CTAM proof material still belongs with the mining-difficulty and proof-format work, not this transport plan.