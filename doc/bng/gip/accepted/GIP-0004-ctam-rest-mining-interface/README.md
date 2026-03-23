# BNG-0004: CTAM REST Mining Interface

**Status**: accepted
**Created**: 2026-03-23

## Abstract
This GIP proposes integrating BNG mining with CTAM over a REST API. The near-term goal is to let the node talk to a local CTAM webservice, likely in a Docker Compose setup, without freezing the final request or response model too early.

## Motivation
CTAM already has some API surface, but not yet a mining-job oriented interface. BNG wants to move in that direction while keeping the proposal open enough for CTAM's existing API and deployment model to evolve.

This GIP is about the integration direction, not the final wire format. Consensus rules for CTAM proofs belong in the mining-difficulty proposal, not here.

## Specification

### 1. Direction
BNG mining should be able to communicate with CTAM through a REST interface instead of assuming CTAM logic is embedded directly inside the node.

The first target is a local deployment where:

- the BNG node runs normally
- a CTAM webservice runs locally, likely through Docker Compose
- the node sends mining-related data to CTAM and receives back enough information to continue mining or validate a candidate

### 2. Scope
This draft does not freeze concrete endpoints or payloads.

Instead, it sets the expectation that:

- CTAM's existing API should be reused where practical
- any missing mining-specific API can be added incrementally
- the node remains responsible for final block construction and validation

### 3. Initial expectations
An initial implementation should be able to:

- connect to a configurable local CTAM REST endpoint
- pass the data CTAM needs for mining on the memory-hard lane
- receive candidate results back from CTAM
- fail cleanly if the CTAM service is unavailable or returns invalid data

## Backwards Compatibility
- Nodes that do not enable CTAM REST integration continue using the existing mining path.
- This proposal is compatible with an incremental rollout where the REST path is introduced first on development networks.

## Security Considerations
- The node must treat CTAM responses as untrusted input.
- A local-only default is preferred for the initial deployment.
- Final proof validation must remain inside the node.

## Deployment / Activation
This should be introduced incrementally:

1. add a basic REST-backed integration path for local development
2. extend CTAM's API only where mining needs it
3. validate the flow on regtest or other development networks before any wider deployment

## Reference Implementation
- mining code that prepares CTAM-facing requests
- a REST client path in the node
- local development tooling for running CTAM alongside the node

## Test Plan
Use the shared build and test commands in `doc/bng/development/README.md`.

Initial coverage should include:

- basic integration tests for node-to-CTAM communication
- failure-path tests when CTAM is unavailable or returns invalid data
- local smoke tests for the intended Docker Compose workflow

## Open Questions
- Which parts of CTAM's current API can be reused directly?
- What additional mining-specific endpoints or payloads are actually needed?
- How much of the first implementation should be standardized in the GIP versus left to implementation notes?

## Changelog
- 2026-03-23: drafted
- 2026-03-23: accepted