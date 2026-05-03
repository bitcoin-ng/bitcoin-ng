# Generation Improvement Proposals (GIPs)

GIPs are the mechanism used by Bitcoin Next-Generation (BNG) to propose, review, and track protocol and implementation changes.

This folder is intentionally small and link-driven: keep GIP documents focused on *what changes* (spec + rationale), and keep *how we work* (build/test/checklists) in shared docs so it doesn’t get copy/pasted into every GIP.

## Repository layout

- `doc/bng/gip/drafted/` – work-in-progress proposals
- `doc/bng/gip/accepted/` – accepted proposals (normative spec)

## How to write a GIP

Start from the template:
- `doc/bng/gip/TEMPLATE.md`

Guidelines:
- Put stable project-wide instructions in shared docs (see links below).
- In the GIP, prefer linking to shared docs instead of duplicating the text.
- If the GIP changes network identity, encoding, or parameters, assume tests/vectors need updating (forks can’t rely on upstream literals).

## Shared instructions (link instead of repeating)

- Build and test locally: `doc/bng/development/README.md`
- Hard-fork / rebrand gotchas (tests, vectors, encodings): `doc/bng/development/hard-fork-notes.md`

## Reusable implementation checklist

Use this checklist in PRs / implementation plans:

1. Update spec text and freeze constants.
2. Implement code changes.
3. Update any tests/vectors that embed network-specific literals (Base58, Bech32 HRPs, BIP32 version bytes).
4. Run targeted tests first (unit test(s) for touched subsystem), then broader suites.
5. Record the exact commands used to validate in the PR description.
