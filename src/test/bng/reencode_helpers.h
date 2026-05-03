// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_BNG_REENCODE_HELPERS_H
#define BITCOIN_TEST_BNG_REENCODE_HELPERS_H

#include <optional>
#include <string>

namespace bng::test {

/**
 * Re-encode a Base58Check token if it looks like:
 * - WIF (1-byte version + 32-byte secret [+ optional 0x01])
 * - BIP32 extended key (4-byte version + 74-byte payload)
 *
 * This is used in tests to keep upstream literals stable across forks that
 * intentionally change network identity (prefix bytes / HRPs).
 */
std::optional<std::string> ReencodeBase58CheckKeyToken(const std::string& token);

/** Replace any Base58Check-encoded WIF/BIP32 keys inside a string. */
std::string ReencodeKeysInString(std::string in);

/**
 * Re-encode WIF/BIP32 keys in a descriptor-like string.
 * If the string contains '#', it is left untouched (checksum would be invalidated).
 */
std::string ReencodeWifKeys(std::string in);

/**
 * For descriptors expected to parse in tests: strip any explicit checksum then
 * rewrite contained keys.
 */
std::string FixupDescriptorForParsing(std::string in);

/** Rewrite keys but preserve any trailing descriptor checksum ("#........"). */
std::string ReencodeWifKeysPreserveChecksum(const std::string& in);

/** Remove a trailing descriptor checksum if present. */
std::string StripChecksumIfPresent(std::string desc);

/** Re-encode common address encodings (Base58 P2PKH/P2SH, Bech32/Bech32m). */
std::optional<std::string> ReencodeAddress(const std::string& address);

/** If the descriptor is addr(...), rewrite the inner address for current network. */
std::string FixupAddrDescriptorForExpectedOutput(const std::string& expected_desc);

} // namespace bng::test

#endif // BITCOIN_TEST_BNG_REENCODE_HELPERS_H
