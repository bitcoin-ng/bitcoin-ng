# BNG Extended Block Header V1

This part of the `GIP-0004` proposes to introduce a new fixed-size BNG extended block header `BNGHeaderV1`  as part of the BNG / Generation consensus rules.

## Motivation

BNG forks Bitcoin Core but requires an extended block header format to support distinct Generation features like history accumulator commitment and advanced PoW parameters.

## Specification

### New BNG Extended Block Header V1 Structure

The new header has a fixed serialized size of 114 bytes with the following fields and layout:

```
version                  4 bytes
prev_block_hash         32 bytes
tx_merkle_root          32 bytes
history_mmr_root        32 bytes
time                     4 bytes
energy_nbits             4 bytes
nonce                    4 bytes
major_work_ctx_size      1 byte
minor_work_rounds        1 byte
--------------------------------
total                  114 bytes
```

Field details:

- `version`: BNG block/header version.
- `prev_block_hash`: Hash of previous BNG extended header.
- `tx_merkle_root`: Merkle root committing to block transactions.
- `history_mmr_root`: History accumulator/MMR root committing to previous block history.
- `time`: Unix timestamp.
- `energy_nbits`: Bitcoin-style compact PoW target representing energy difficulty.
- `nonce`: 32-bit mining nonce.
- `major_work_ctx_size`: Major work/context-size difficulty parameter, interpreted as a compact level/exponent.
- `minor_work_rounds`: Minor work rounds; max 64, consensus initial value 16.

### Offsets

```
version                  0..3
prev_block_hash          4..35
tx_merkle_root          36..67
history_mmr_root        68..99
time                  100..103
energy_nbits          104..107
nonce                 108..111
major_work_ctx_size   112
minor_work_rounds     113
```

### Constants

```cpp
static constexpr size_t BNG_HEADER_V1_SIZE = 114;
static constexpr size_t BNG_HEADER_V1_NONCE_OFFSET = 108;
static constexpr uint8_t BNG_MINOR_WORK_ROUNDS_MAX = 64;
static constexpr uint8_t BNG_MINOR_WORK_ROUNDS_FIXED = 16;
```

### Hashing Rule

The block hash is computed by hashing the full serialized 114-byte Generation header (not just the legacy 80-byte prefix).

### Validation Rules

- Serialized header size must be exactly 114 bytes.
- `minor_work_rounds` must be <= 64.
- Initial consensus requires `minor_work_rounds` to be exactly 16.
- `major_work_ctx_size` is interpreted as a compact context-size difficulty.
- `energy_nbits` remains the Bitcoin-style compact target difficulty.

### Mining Work

- Work input is the full serialized 114-byte Generation header.
- Nonce offset at byte 108.

## Restrictions

- No CTAM fields (`ctam_*`) added.
- No `work_algo_id`, `feature_flags`, or reserved padding.
- CTAM mining or BitVM semantics are not part of this task.

## Tests

- Serialization roundtrip correctness.
- Serialized size verification (114 bytes).
- Nonce offset verification (108).
- Changing `history_mmr_root` affects the block hash.
- Changing `major_work_ctx_size` affects the block hash.
- Changing `minor_work_rounds` affects the block hash.
- Validation failure if `minor_work_rounds` > 64.
- Initial consensus validation fails if `minor_work_rounds` != 16.
- Hash path uses all 114 bytes, not legacy 80 bytes.

## Implementation Plan

1. Define `BNGHeader` as a C++ struct/class with exact serialization size 114.
2. Implement serialization, deserialization with strict size checks.
3. Implement hashing that uses the full 114-byte serialized header.
4. Implement validation logic as specified.
5. Add constants for sizes and offsets.
6. Implement mining work input rules and nonce offset.
7. Write comprehensive unit tests covering serialization, hashing, and validation.
8. Exclude any CTAM, work_algo_id, feature_flags, or padding fields from this implementation.

---
# IMPLEMENTATION PLAN

> No parallel `BNGHeader`, no compatibility shim.

Use `CBlockHeader` as the canonical BNG 114-byte header and hard-patch the existing Bitcoin header shape in place.

**Plan**

1. Extend `CBlockHeader` directly in `src/primitives/block.h`:
   ```cpp
   int32_t nVersion;
   uint256 hashPrevBlock;
   uint256 hashMerkleRoot;
   uint256 hashHistoryMMRRoot;
   uint32_t nTime;
   uint32_t nBits;        // keep name; this is energy_nbits
   uint32_t nNonce;
   uint8_t nMajorWorkCtxSize;
   uint8_t nMinorWorkRounds;
   ```

2. Change `SERIALIZE_METHODS(CBlockHeader, ...)` to serialize exactly that order:
   `version, prev, tx merkle, history mmr, time, nBits, nonce, major, minor`.

3. Add hard constants, probably near `CBlockHeader`:
   ```cpp
   static constexpr size_t BNG_HEADER_V1_SIZE = 114;
   static constexpr size_t BNG_HEADER_V1_NONCE_OFFSET = 108;
   static constexpr uint8_t BNG_MINOR_WORK_ROUNDS_MAX = 64;
   static constexpr uint8_t BNG_MINOR_WORK_ROUNDS_FIXED = 16;
   ```

4. Keep `CBlockHeader::GetHash()` unchanged structurally:
   ```cpp
   return (HashWriter{} << *this).GetHash();
   ```
   Once serialization is patched, hash automatically uses all 114 bytes.

5. Patch `CBlock`, `CBlockIndex`, and `CDiskBlockIndex` to carry the new fields. This is mandatory because index restart reconstructs hashes from stored header fields.

6. Patch validation in `CheckBlockHeader` / contextual header checks:
   - `nMinorWorkRounds <= 64`
   - `nMinorWorkRounds == 16`
   - serialized header size is `114`
   - keep `nBits` difficulty behavior as energy difficulty
   - CTAM difficulty uses `nMajorWorkCtxSize` and `nMinorWorkRounds` from the header, not live/default external values.

7. Patch mining/template construction:
   - set `hashHistoryMMRRoot` initially to the real accumulator root when available, otherwise a deterministic placeholder/null during first pass
   - set `nMajorWorkCtxSize`
   - set `nMinorWorkRounds = 16`
   - keep `nNonce = 0`

8. Patch CTAM mining work:
   - `BuildCtamHeaderMiningWork()` should still serialize `CBlockHeader`
   - assert work size `114`
   - nonce offset should be hardcoded/checked as `108`, not computed as “last 4 bytes”, because nonce is no longer at the end.

9. Patch RPC/P2P/test framework assumptions:
   - `getblockheader`, REST, submitheader, headers messages now serialize 114-byte headers
   - Python `test_framework.messages.CBlockHeader` gets the same fields/order
   - replace `80` byte / `160` hex assumptions with `114` / `228`
   - CTAM functional tests update nonce offset `76 -> 108`.

10. Patch genesis and chainparams:
   - add `hashHistoryMMRRoot`, `nMajorWorkCtxSize`, `nMinorWorkRounds`
   - recompute genesis hash assertions
   - old datadirs/indexes are intentionally incompatible.

That is the cleaner hardfork move: same class names, same broad call graph, but the old 80-byte Bitcoin header simply stops existing in BNG.