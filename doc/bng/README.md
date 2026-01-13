# Bitcoin-NG Documentation

Welcome to the documentation for **Bitcoin-NG** (BNG), a fork of Bitcoin Core.

## Overview
This directory contains all documentation related to the BNG project, including development guides, design proposals (Issues/BIPs), and the changelog.

## Roadmap
The current roadmap for Bitcoin-NG is as follows:

- [ ] **Rebranding**
    - [ ] New address space - see `issues/draft/BNG-0001-rebranding.md`
    - [ ] Magic bytes modification - see `issues/draft/BNG-0001-rebranding.md`
    - [ ] Default port changes - see `issues/draft/BNG-0001-rebranding.md`
    - [ ] Replay attack protection - see `issues/draft/BNG-0002-prevent-replay_attacks.md`
- [ ] **Consensus Changes**
    - [ ] Fork UTXO set (UTXO snapshot) - see `issues/draft/BNG-0003-utxo_snapshot.md`
    - [ ] Mining modification - see `issues/draft/BNG-0004-mining-modification.md`
    - [ ] Difficulty adjustment algorithm (DAA) changes - see `issues/draft/BNG-0005-difficulty-adjustment.md`
- [ ] **Documentation**
    - [ ] Whitepaper

## Directory Structure

- **[`changes/`](./changes/)**: Changelog and release notes.
- **[`issues/`](./issues/)**: Design proposals and issue tracking (BNG-XXXX).
    - `draft/`: Proposals currently under discussion.
    - `open/`: Active proposals approved for implementation.
    - `resolved/`: Completed or rejected proposals.
- **[`references/`](./references/)**: Guides, manuals, and technical references.
    - `develop.md`: Development setup and instructions.
    - `testing/`: Testing strategies and guides.
- **[`archive/`](./archive/)**: Archived documentation and legacy files.


Again, for development setup and contribution guidelines, see `references/develop.md`.