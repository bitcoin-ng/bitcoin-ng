// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/bng/reencode_helpers.h>

#include <addresstype.h>
#include <base58.h>
#include <bech32.h>
#include <key.h>
#include <key_io.h>
#include <util/strencodings.h>

#include <algorithm>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace bng::test {

std::optional<std::string> ReencodeBase58CheckKeyToken(const std::string& token)
{
    // Decode Base58Check into raw bytes. We intentionally don't validate network prefixes here;
    // instead, we recognize plausible key encodings and re-encode for the current network.
    std::vector<unsigned char> data;
    if (!DecodeBase58Check(token, data, /*max_ret_len=*/100)) return std::nullopt;

    // WIF: <version><32-byte secret>[<0x01 if compressed>]
    if (data.size() == 33 || data.size() == 34) {
        const bool compressed = (data.size() == 34);
        if (compressed && data.back() != 1) return std::nullopt;

        CKey key;
        key.Set(data.begin() + 1, data.begin() + 33, compressed);
        if (!key.IsValid()) return std::nullopt;

        return EncodeSecret(key);
    }

    // BIP32 extended keys: <4-byte version><74-byte payload>
    if (data.size() == BIP32_EXTKEY_WITH_VERSION_SIZE) {
        constexpr size_t VERSION_SIZE{4};
        const unsigned char* payload = data.data() + VERSION_SIZE;

        // payload layout: depth(1) | fp(4) | child(4) | chaincode(32) | keydata(33)
        const unsigned char keydata_prefix = payload[41];
        if (keydata_prefix == 0x00) {
            CExtKey extkey;
            extkey.Decode(payload);
            if (!extkey.key.IsValid()) return std::nullopt;
            return EncodeExtKey(extkey);
        }
        if (keydata_prefix == 0x02 || keydata_prefix == 0x03) {
            CExtPubKey extpub;
            extpub.Decode(payload);
            if (!extpub.pubkey.IsValid()) return std::nullopt;
            return EncodeExtPubKey(extpub);
        }
    }

    return std::nullopt;
}

std::string ReencodeKeysInString(std::string in)
{
    // Replace any Base58Check-encoded WIF or BIP32 extended keys with encodings valid for the
    // current network. This keeps tests stable across forks that change Base58 prefixes.
    static const std::regex base58_re{"[123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz]{20,}"};

    std::string out;
    out.reserve(in.size());
    size_t last{0};
    for (auto it = std::sregex_iterator{in.begin(), in.end(), base58_re}; it != std::sregex_iterator{}; ++it) {
        const auto& match{*it};
        const size_t pos{static_cast<size_t>(match.position())};
        const size_t len{static_cast<size_t>(match.length())};
        out.append(in, last, pos - last);
        const std::string token{match.str()};
        if (auto maybe = ReencodeBase58CheckKeyToken(token)) {
            out.append(*maybe);
        } else {
            out.append(token);
        }
        last = pos + len;
    }
    out.append(in, last, std::string::npos);
    return out;
}

std::string ReencodeWifKeys(std::string in)
{
    // Do not touch descriptors with explicit checksums; rewriting would invalidate the checksum.
    if (in.find('#') != std::string::npos) return in;
    return ReencodeKeysInString(std::move(in));
}

std::string FixupDescriptorForParsing(std::string in)
{
    // For descriptors that are expected to parse successfully in tests, strip any explicit checksum
    // before rewriting keys. This avoids having to recompute checksums.
    if (const size_t hash_pos = in.find('#'); hash_pos != std::string::npos) {
        in.erase(hash_pos);
    }
    return ReencodeKeysInString(std::move(in));
}

std::string ReencodeWifKeysPreserveChecksum(const std::string& in)
{
    const size_t hash_pos = in.find('#');
    if (hash_pos == std::string::npos) return ReencodeKeysInString(in);
    return ReencodeKeysInString(in.substr(0, hash_pos)) + in.substr(hash_pos);
}

std::string StripChecksumIfPresent(std::string desc)
{
    if (desc.size() > 9 && desc[desc.size() - 9] == '#') desc.erase(desc.size() - 9);
    return desc;
}

std::optional<std::string> ReencodeAddress(const std::string& address)
{
    // Base58 P2PKH/P2SH (network prefixes differ across networks/forks).
    std::vector<unsigned char> data;
    if (DecodeBase58Check(address, data, /*max_ret_len=*/100) && data.size() == 21) {
        uint160 hash;
        std::copy(data.begin() + 1, data.end(), hash.begin());
        const unsigned char ver = data[0];
        if (ver == 0x00 || ver == 0x6f) return EncodeDestination(PKHash(hash));
        if (ver == 0x05 || ver == 0xc4) return EncodeDestination(ScriptHash(hash));
    }

    // Bech32/Bech32m (HRP differs across networks/forks).
    const auto dec = bech32::Decode(address);
    if (dec.encoding == bech32::Encoding::BECH32 || dec.encoding == bech32::Encoding::BECH32M) {
        if (dec.data.empty()) return std::nullopt;
        const int version = dec.data[0];

        std::vector<unsigned char> program;
        program.reserve(((dec.data.size() - 1) * 5) / 8);
        if (!ConvertBits<5, 8, false>([&](unsigned char c) { program.push_back(c); }, dec.data.begin() + 1, dec.data.end())) {
            return std::nullopt;
        }

        if (version == 0 && program.size() == 20) {
            WitnessV0KeyHash id;
            std::copy(program.begin(), program.end(), id.begin());
            return EncodeDestination(id);
        }
        if (version == 0 && program.size() == 32) {
            WitnessV0ScriptHash id;
            std::copy(program.begin(), program.end(), id.begin());
            return EncodeDestination(id);
        }
        if (version == 1 && program.size() == 32) {
            WitnessV1Taproot id;
            std::copy(program.begin(), program.end(), id.begin());
            return EncodeDestination(id);
        }
    }

    return std::nullopt;
}

std::string FixupAddrDescriptorForExpectedOutput(const std::string& expected_desc)
{
    // Only needed for the addr(...) descriptors inferred from scripts.
    static constexpr std::string_view prefix{"addr("};
    if (!expected_desc.starts_with(prefix) || expected_desc.size() < prefix.size() + 2 || expected_desc.back() != ')') {
        return expected_desc;
    }
    const std::string inner = expected_desc.substr(prefix.size(), expected_desc.size() - prefix.size() - 1);
    if (auto rewritten = ReencodeAddress(inner)) {
        return "addr(" + *rewritten + ")";
    }
    return expected_desc;
}

} // namespace bng::test
