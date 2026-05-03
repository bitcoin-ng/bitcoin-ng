// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <base58.h>
#include <chainparams.h>
#include <key.h>
#include <key_io.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstring>
#include <vector>

namespace {

constexpr std::array<unsigned char, 4> BITCOIN_XPUB{{0x04, 0x88, 0xB2, 0x1E}};
constexpr std::array<unsigned char, 4> BITCOIN_XPRV{{0x04, 0x88, 0xAD, 0xE4}};
constexpr std::array<unsigned char, 4> BITCOIN_TPUB{{0x04, 0x35, 0x87, 0xCF}};
constexpr std::array<unsigned char, 4> BITCOIN_TPRV{{0x04, 0x35, 0x83, 0x94}};

std::vector<unsigned char> AsVector(const std::array<unsigned char, 4>& a)
{
    return {a.begin(), a.end()};
}

void CheckPrefix(const std::vector<unsigned char>& actual, const std::array<unsigned char, 4>& expected)
{
    BOOST_REQUIRE_EQUAL(actual.size(), expected.size());
    BOOST_CHECK(std::equal(expected.begin(), expected.end(), actual.begin()));
}

std::vector<unsigned char> DecodeBase58Check78(const std::string& s)
{
    std::vector<unsigned char> decoded;
    BOOST_REQUIRE(DecodeBase58Check(s, decoded, 78));
    BOOST_REQUIRE_EQUAL(decoded.size(), 78);
    return decoded;
}

void CheckEncodeDecodePayloadMatchesChainParams(const CExtKey& key)
{
    const auto& prefix = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    BOOST_REQUIRE_EQUAL(prefix.size(), 4U);

    const std::string encoded = EncodeExtKey(key);
    const auto decoded = DecodeBase58Check78(encoded);

    BOOST_CHECK(std::equal(prefix.begin(), prefix.end(), decoded.begin()));

    unsigned char payload[BIP32_EXTKEY_SIZE];
    key.Encode(payload);
    BOOST_CHECK(std::memcmp(payload, decoded.data() + prefix.size(), BIP32_EXTKEY_SIZE) == 0);

    BOOST_CHECK(DecodeExtKey(encoded) == key);
}

void CheckEncodeDecodePayloadMatchesChainParams(const CExtPubKey& key)
{
    const auto& prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    BOOST_REQUIRE_EQUAL(prefix.size(), 4U);

    const std::string encoded = EncodeExtPubKey(key);
    const auto decoded = DecodeBase58Check78(encoded);

    BOOST_CHECK(std::equal(prefix.begin(), prefix.end(), decoded.begin()));

    unsigned char payload[BIP32_EXTKEY_SIZE];
    key.Encode(payload);
    BOOST_CHECK(std::memcmp(payload, decoded.data() + prefix.size(), BIP32_EXTKEY_SIZE) == 0);

    BOOST_CHECK(DecodeExtPubKey(encoded) == key);
}

std::vector<std::byte> DeterministicSeed()
{
    return ParseHex<std::byte>("000102030405060708090a0b0c0d0e0f");
}

} // namespace

BOOST_AUTO_TEST_SUITE(bng_bip32_identity_tests)

BOOST_AUTO_TEST_CASE(extkey_prefix_bytes_main)
{
    BasicTestingSetup setup{ChainType::MAIN};
    const CChainParams& params = Params();

    // BNG mainnet: must be distinct from Bitcoin mainnet (xpub/xprv).
    CheckPrefix(params.Base58Prefix(CChainParams::EXT_PUBLIC_KEY), {0x04, 0x5f, 0x1c, 0xf6});
    CheckPrefix(params.Base58Prefix(CChainParams::EXT_SECRET_KEY), {0x04, 0x5f, 0x18, 0xbc});
    BOOST_CHECK(params.Base58Prefix(CChainParams::EXT_PUBLIC_KEY) != AsVector(BITCOIN_XPUB));
    BOOST_CHECK(params.Base58Prefix(CChainParams::EXT_SECRET_KEY) != AsVector(BITCOIN_XPRV));

    CExtKey master;
    master.SetSeed(DeterministicSeed());
    CheckEncodeDecodePayloadMatchesChainParams(master);
    CheckEncodeDecodePayloadMatchesChainParams(master.Neuter());
}

BOOST_AUTO_TEST_CASE(extkey_prefix_bytes_testnet)
{
    BasicTestingSetup setup{ChainType::TESTNET};
    const CChainParams& params = Params();

    // BNG testnet prefixes are explicitly defined for GIP-0001 testnet address space.
    CheckPrefix(params.Base58Prefix(CChainParams::EXT_PUBLIC_KEY), {0x04, 0x35, 0x12, 0x34});
    CheckPrefix(params.Base58Prefix(CChainParams::EXT_SECRET_KEY), {0x04, 0x35, 0x43, 0x21});

    CExtKey master;
    master.SetSeed(DeterministicSeed());
    CheckEncodeDecodePayloadMatchesChainParams(master);
    CheckEncodeDecodePayloadMatchesChainParams(master.Neuter());
}

BOOST_AUTO_TEST_CASE(extkey_prefix_bytes_regtest_policy)
{
    // Regtest is local-only, and may intentionally reuse upstream testnet/regtest version bytes.
    BasicTestingSetup setup{ChainType::REGTEST};
    const CChainParams& params = Params();

    CheckPrefix(params.Base58Prefix(CChainParams::EXT_PUBLIC_KEY), BITCOIN_TPUB);
    CheckPrefix(params.Base58Prefix(CChainParams::EXT_SECRET_KEY), BITCOIN_TPRV);

    CExtKey master;
    master.SetSeed(DeterministicSeed());
    CheckEncodeDecodePayloadMatchesChainParams(master);
    CheckEncodeDecodePayloadMatchesChainParams(master.Neuter());
}

BOOST_AUTO_TEST_SUITE_END()
