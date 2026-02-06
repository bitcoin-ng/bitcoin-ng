# Copilot instructions (bitcoin-ng / BNG docs)

These instructions are optimized for making correct, minimal changes in **BNG documentation**, especially under `doc/bng/`.

## Primary scope: `doc/bng/`
- Treat `doc/bng/` as the source of truth for BNG-specific behavior (fork rationale, network identity, GIPs).
- Keep edits **link-driven**: GIPs should describe the *spec + rationale* and link to shared workflow docs instead of duplicating them.

## Doc layout and intent
- `doc/bng/README.md`: high-level project identity (BNG vs upstream), branch/version conventions.
- `doc/bng/development/README.md`: build/test commands and environment notes.
- `doc/bng/development/hard-fork-notes.md`: fork-specific “gotchas” (test vectors, encodings, network identity ripple effects).
- `doc/bng/gip/`: Generation Improvement Proposals.
  - Drafts: `doc/bng/gip/drafted/`
  - Accepted (normative): `doc/bng/gip/accepted/`
  - Template: `doc/bng/gip/TEMPLATE.md`

## Writing/Updating a GIP
- Start from `doc/bng/gip/TEMPLATE.md` and keep the header fields consistent:
  - `BNG-XXXX` prefix, `**Status**`, and `**Created**: YYYY-MM-DD`.
- Prefer **stable shared docs** for workflow content:
  - Build/test instructions → `doc/bng/development/README.md`
  - Hard-fork test/vector guidance → `doc/bng/development/hard-fork-notes.md`

## Fork-specific correctness rules (important)
- Assume many upstream literals/vectors are intentionally wrong for BNG when they embed Bitcoin network identity.
- If documenting or changing anything related to network identity (magic bytes, ports, Base58 prefixes, Bech32 HRPs, BIP32 xpub/xprv version bytes, genesis), cross-check guidance in `doc/bng/development/hard-fork-notes.md`.
- When discussing tests/vectors, prefer guidance that compares **decoded bytes/structures** rather than asserting network-encoded Base58/Bech32 strings.

## Build/test snippets in docs
- Use the CMake-first flow shown in `doc/bng/development/README.md` (no top-level `./autogen.sh`).
- If mentioning IPC/multiprocess, note that `-DENABLE_IPC=ON` requires Cap’n Proto on Ubuntu.

## Style and PR hygiene for docs
- Keep Markdown minimal and scannable (short sections, bullets).
- Some existing GIPs embed historical discussion blocks; avoid adding new large pasted discussions—prefer linking to a GIP/doc section or leaving context in git history.
