# BIP32 / extended keys notes (GIP-0001)

BNG changes network identity parameters (GIP-0001), including Base58 version bytes.
This impacts how BIP32 extended keys are *encoded as strings*.

## Background: what’s network-dependent vs network-independent

BIP32 extended keys have a 78-byte serialization before Base58Check:

- `version` (4 bytes)
- `depth` (1)
- `parent fingerprint` (4)
- `child number` (4)
- `chain code` (32)
- `key data` (33)

The **version** bytes are used as a network identity / address-space selector in practice.
They are what makes an encoded extended key start with recognizable prefixes like `xpub`/`xprv` (Bitcoin mainnet) or `tpub`/`tprv` (Bitcoin test/regtest).

BNG intentionally sets different version bytes for its own networks in `src/kernel/chainparams.cpp`.

## Why upstream BIP32 test vectors fail in a fork

Upstream `bip32_tests` historically compares derived keys against literal Base58 strings from the canonical BIP32 vectors (e.g. `xpub...` / `xprv...`).

That comparison implicitly assumes Bitcoin mainnet version bytes:

- xpub version = `0x0488B21E`
- xprv version = `0x0488ADE4`

If BNG changes the chainparams version bytes, `EncodeExtPubKey`/`EncodeExtKey` must produce different strings even when derivation is correct.
So a strict string comparison becomes a test of **chain identity encoding**, not BIP32 derivation.

## What changed in tests

To keep responsibilities clean:

- [src/test/bip32_tests.cpp](src/test/bip32_tests.cpp) now validates BIP32 vectors by Base58Check-decoding the vector strings and comparing the **74-byte payload** (everything except the 4-byte version).
- It still includes lightweight roundtrip checks to ensure BNG’s key I/O glue remains correct:
  - `DecodeExtKey(EncodeExtKey(k)) == k`
  - `DecodeExtPubKey(EncodeExtPubKey(pk)) == pk`

Separately, BNG-specific tests can pin chain identity expectations (the version bytes) without tying BIP32 derivation tests to specific human-facing strings.

See also: guidance in `doc/bng/development/hard-fork-notes.md` about preferring decoded-bytes assertions over network-encoded strings when network identity differs.
