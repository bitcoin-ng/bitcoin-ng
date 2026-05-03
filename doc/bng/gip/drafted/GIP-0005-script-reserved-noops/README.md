# BNG-0005: Script Reserved No-Op Opcodes

**Status**: drafted
**Created**: 2026-03-23

## Abstract
This GIP proposes extending Bitcoin Script with a BNG-reserved family of opcodes that are introduced initially as explicit no-ops. The purpose is to carve out upgrade space for future script features without changing stack behavior today.

## Motivation
BNG expects future script evolution, but repeatedly reusing whatever opcode space happens to remain leads to ad hoc upgrades and poor signaling discipline.

Introducing a named set of reserved BNG opcodes now provides:

- a documented expansion area for future script work
- early parser, disassembler, and test coverage for the opcode family
- a way to stage future soft-fork or policy changes without inventing names later

The initial rollout deliberately keeps the behavior simple: each new opcode behaves exactly like a no-op.

## Specification

### 1. Reserved opcode family
Define a contiguous set of BNG-specific reserved opcodes:

- `OP_BNGNOP1`
- `OP_BNGNOP2`
- `OP_BNGNOP3`
- `OP_BNGNOP4`
- `OP_BNGNOP5`
- `OP_BNGNOP6`
- `OP_BNGNOP7`
- `OP_BNGNOP8`

The concrete opcode byte values MUST be selected from unassigned values that do not collide with currently valid Bitcoin Script opcodes or BNG extensions already in use. The final byte assignments remain open in this draft and must be frozen before acceptance.

### 2. Initial execution semantics
Until a later GIP assigns stronger meaning, each `OP_BNGNOPx` behaves as follows:

- consumes no stack items
- pushes no stack items
- does not alter the altstack
- does not change the script error state
- advances the program counter exactly as any single-byte opcode would

In other words, the runtime behavior is identical to `OP_NOP`.

### 3. Script validity
For consensus evaluation after activation:

- scripts containing `OP_BNGNOPx` remain valid so long as they would otherwise be valid
- these opcodes do not by themselves make a script non-standard or non-final

Policy may still discourage their use in relay by default until wallet and tooling support is in place. If BNG keeps a discouragement flag analogous to `SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS`, it should be extended to cover the new family in a configurable way.

### 4. Tooling visibility
Script formatting and diagnostics should recognize the new symbolic names.

Minimum changes:

- script disassembly prints `OP_BNGNOPx`
- test fixtures and debug output preserve the symbolic names
- documentation treats the family as reserved upgrade space, not as currently useful contract features

### 5. Future upgrades
Any later proposal that repurposes one of these opcodes MUST specify:

- activation mechanism
- consensus semantics
- policy behavior before and after activation
- expected effect on tapscript, legacy script, or both

This GIP does not pre-authorize any future semantic change. It only reserves names and initial no-op behavior.

## Backwards Compatibility
- Pre-activation nodes that do not recognize the byte values will reject scripts using them if the chosen values are currently treated as invalid.
- Because of that, activation must be handled as an intentional consensus and policy deployment, even though the runtime semantics are no-op after activation.
- Existing scripts that do not use the new opcodes are unaffected.

## Security Considerations
- Choosing opcode byte values carelessly could collide with existing or disabled semantics and create consensus risk.
- Treating the family as no-op today reduces immediate execution complexity, but it can also invite premature use in outputs that later gain stronger meaning.
- Standardness policy should be conservative until the ecosystem has clear upgrade guidance.

## Deployment / Activation
Recommended rollout:

1. audit the remaining opcode map and freeze byte assignments
2. update parser, assembler, disassembler, and script tests
3. activate on regtest and a BNG test network first
4. decide whether mainnet policy should relay these opcodes immediately or only after a later upgrade uses them

## Reference Implementation
- `src/script/opcodetype.{h,cpp}` for opcode definitions and names
- `src/script/interpreter.cpp` for execution semantics
- `src/test/script_*` and any JSON script fixtures for parser and execution coverage
- wallet and RPC surfaces that render script asm text

## Test Plan
Use the shared build and test commands in `doc/bng/development/README.md`.

Coverage should include:

- parser tests for each `OP_BNGNOPx` mnemonic
- execution tests proving stack and altstack are unchanged
- policy tests for default relay behavior if discouragement remains enabled
- round-trip asm and hex serialization tests

## Open Questions
- Which exact opcode byte values are safest to reserve?
- Should the family be available in both legacy script and tapscript from the start?
- Should relay policy initially discourage these opcodes even though consensus accepts them?
- Is eight reserved opcodes the right initial size, or should the family be smaller?

## Changelog
- 2026-03-23: drafted