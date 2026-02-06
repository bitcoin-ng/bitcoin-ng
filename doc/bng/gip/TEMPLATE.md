# BNG-XXXX: <Title>

**Status**: drafted | accepted | rejected | withdrawn
**Created**: YYYY-MM-DD

## Abstract
One paragraph summary of the change.

## Motivation
Why the change is needed. Include fork-specific concerns (network isolation, replay risk, operational safety) when relevant.

## Specification
Normative description of behavior and constants.

- Parameters / constants
- Serialization formats (if any)
- Consensus and/or policy rules (if any)
- Network protocol / P2P (if any)
- Wallet / RPC surface changes (if any)

## Backwards Compatibility
What breaks, what stays compatible, and any migration requirements.

## Security Considerations
Threat model changes, new attack surface, and mitigations.

## Deployment / Activation
How the change rolls out (flags, activation height/time, compatibility windows).

## Reference Implementation
- File(s) and module(s) to change
- Expected test updates
- Any tooling needed to regenerate vectors

## Test Plan
Concrete commands and expected outputs.

Example:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
ctest --test-dir build -R <relevant_suite> --output-on-failure
```

## Changelog
- YYYY-MM-DD: drafted
- YYYY-MM-DD: accepted
