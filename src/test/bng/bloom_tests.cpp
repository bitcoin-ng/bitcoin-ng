// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <common/bloom.h>
#include <key.h>
#include <key_io.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <string>
#include <vector>

using namespace util::hex_literals;

BOOST_AUTO_TEST_SUITE(bng_bloom_tests)

namespace {

// Bitcoin mainnet WIF for an *uncompressed* private key (prefix 0x80).
// We use it only as a stable container for 32 key bytes, then re-encode under BNG.
constexpr const char* BITCOIN_MAINNET_WIF_UNCOMPRESSED =
    "5Kg1gnAjaLfKiwhhPpGS3QfRg2m6awQvaj98JCZBZQ5SuS2F15C";

std::array<unsigned char, 32> ExtractKeyBytesFromWifPayload(const std::string& wif)
{
    std::vector<unsigned char> data;
    BOOST_REQUIRE(DecodeBase58Check(wif, data, 34));
    // Expect 1-byte prefix + 32 key bytes (uncompressed WIF).
    BOOST_REQUIRE_EQUAL(data.size(), 33U);

    std::array<unsigned char, 32> key_bytes;
    std::copy_n(data.data() + 1, key_bytes.size(), key_bytes.begin());
    return key_bytes;
}

void CheckWifPrefixByte(const std::string& wif, unsigned char expected_prefix)
{
    std::vector<unsigned char> decoded;
    BOOST_REQUIRE(DecodeBase58Check(wif, decoded, 34));
    BOOST_REQUIRE(decoded.size() == 33U || (decoded.size() == 34U && decoded.back() == 1));
    BOOST_CHECK_EQUAL(decoded[0], expected_prefix);
}

} // namespace

BOOST_AUTO_TEST_CASE(bng_mainnet_wif_decode_and_bloom_serialization)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const auto key_bytes = ExtractKeyBytesFromWifPayload(BITCOIN_MAINNET_WIF_UNCOMPRESSED);

    CKey key;
    key.Set(key_bytes.begin(), key_bytes.end(), /*fCompressedIn=*/false);
    BOOST_REQUIRE(key.IsValid());

    // Encode the key under BNG mainnet rules and assert the BNG prefix byte is present.
    const std::string bng_wif = EncodeSecret(key);
    CheckWifPrefixByte(bng_wif, /*expected_prefix=*/166);
    BOOST_REQUIRE(DecodeSecret(bng_wif).IsValid());
    BOOST_CHECK(DecodeSecret(bng_wif) == key);

    // The original Bitcoin WIF must be rejected under BNG mainnet.
    BOOST_CHECK(!DecodeSecret(BITCOIN_MAINNET_WIF_UNCOMPRESSED).IsValid());

    // Bloom filter behavior should remain stable for the derived pubkey/hash.
    const CPubKey pubkey = key.GetPubKey();
    std::vector<unsigned char> vchPubKey(pubkey.begin(), pubkey.end());

    CBloomFilter filter(2, 0.001, 0, BLOOM_UPDATE_ALL);
    filter.insert(vchPubKey);
    filter.insert(pubkey.GetID());

    DataStream stream{};
    stream << filter;

    constexpr auto expected{"038fc16b080000000000000001"_hex};
    BOOST_CHECK_EQUAL_COLLECTIONS(stream.begin(), stream.end(), expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(bng_testnet_wif_prefix_roundtrip)
{
    BasicTestingSetup setup{ChainType::TESTNET};

    const auto key_bytes = ExtractKeyBytesFromWifPayload(BITCOIN_MAINNET_WIF_UNCOMPRESSED);

    CKey key;
    key.Set(key_bytes.begin(), key_bytes.end(), /*fCompressedIn=*/false);
    BOOST_REQUIRE(key.IsValid());

    const std::string bng_wif = EncodeSecret(key);
    CheckWifPrefixByte(bng_wif, /*expected_prefix=*/228);
    BOOST_REQUIRE(DecodeSecret(bng_wif).IsValid());
    BOOST_CHECK(DecodeSecret(bng_wif) == key);
}

BOOST_AUTO_TEST_SUITE_END()
