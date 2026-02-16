// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <arith_uint256.h>
#include <chainparams.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace {

void CheckBase58Prefix1(const CChainParams& params, const CChainParams::Base58Type type, const unsigned char expected)
{
    const auto& prefix = params.Base58Prefix(type);
    BOOST_REQUIRE_EQUAL(prefix.size(), 1U);
    BOOST_CHECK_EQUAL(prefix[0], expected);
}

void CheckBase58Prefix4(const CChainParams& params, const CChainParams::Base58Type type, const std::array<unsigned char, 4>& expected)
{
    const auto& prefix = params.Base58Prefix(type);
    BOOST_REQUIRE_EQUAL(prefix.size(), expected.size());
    BOOST_CHECK(std::equal(prefix.begin(), prefix.end(), expected.begin()));
}

void CheckPowLimitMatchesBits(const CChainParams& params, const uint32_t bits)
{
    const auto pow_limit = UintToArith256(params.GetConsensus().powLimit);

    bool negative{false};
    bool overflow{false};
    arith_uint256 expected_target;
    expected_target.SetCompact(bits, &negative, &overflow);

    BOOST_CHECK(!negative);
    BOOST_CHECK(!overflow);
    BOOST_CHECK(pow_limit == expected_target);
}

} // namespace

BOOST_AUTO_TEST_SUITE(bng_network_identity_values_tests)

BOOST_AUTO_TEST_CASE(mainnet_values_pinned)
{
    BasicTestingSetup setup{ChainType::MAIN};
    const CChainParams& params = Params();

    const auto msg = params.MessageStart();
    BOOST_REQUIRE_EQUAL(msg[0], 0x42);
    BOOST_REQUIRE_EQUAL(msg[1], 0x4e);
    BOOST_REQUIRE_EQUAL(msg[2], 0x47);
    BOOST_REQUIRE_EQUAL(msg[3], 0x01);

    BOOST_REQUIRE_EQUAL(params.GetDefaultPort(), 9333);
    BOOST_REQUIRE_EQUAL(params.Bech32HRP(), "bng");

    // GIP-0001 mainnet address space
    CheckBase58Prefix1(params, CChainParams::PUBKEY_ADDRESS, 38);
    CheckBase58Prefix1(params, CChainParams::SCRIPT_ADDRESS, 23);
    CheckBase58Prefix1(params, CChainParams::SECRET_KEY, 166);

    // Pinned by src/kernel/chainparams.cpp
    CheckBase58Prefix4(params, CChainParams::EXT_PUBLIC_KEY, {0x04, 0x5f, 0x1c, 0xf6});
    CheckBase58Prefix4(params, CChainParams::EXT_SECRET_KEY, {0x04, 0x5f, 0x18, 0xbc});

    // Genesis / PoW compatibility
    BOOST_REQUIRE_EQUAL(params.GenesisBlock().nBits, 0x1f00ffffU);
    CheckPowLimitMatchesBits(params, params.GenesisBlock().nBits);

    BOOST_CHECK(GetNetworkForMagic(params.MessageStart()) == ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(testnet_values_pinned)
{
    BasicTestingSetup setup{ChainType::TESTNET};
    const CChainParams& params = Params();

    const auto msg = params.MessageStart();
    BOOST_REQUIRE_EQUAL(msg[0], 0x42);
    BOOST_REQUIRE_EQUAL(msg[1], 0x4e);
    BOOST_REQUIRE_EQUAL(msg[2], 0x47);
    BOOST_REQUIRE_EQUAL(msg[3], 0x02);

    BOOST_REQUIRE_EQUAL(params.GetDefaultPort(), 19333);
    BOOST_REQUIRE_EQUAL(params.Bech32HRP(), "tbng");

    // GIP-0001 testnet address space
    CheckBase58Prefix1(params, CChainParams::PUBKEY_ADDRESS, 100);
    CheckBase58Prefix1(params, CChainParams::SCRIPT_ADDRESS, 110);
    CheckBase58Prefix1(params, CChainParams::SECRET_KEY, 228);

    CheckBase58Prefix4(params, CChainParams::EXT_PUBLIC_KEY, {0x04, 0x35, 0x12, 0x34});
    CheckBase58Prefix4(params, CChainParams::EXT_SECRET_KEY, {0x04, 0x35, 0x43, 0x21});

    BOOST_REQUIRE_EQUAL(params.GenesisBlock().nBits, 0x1f00ffffU);
    CheckPowLimitMatchesBits(params, params.GenesisBlock().nBits);

    BOOST_CHECK(GetNetworkForMagic(params.MessageStart()) == ChainType::TESTNET);
}

BOOST_AUTO_TEST_CASE(regtest_values_pinned)
{
    BasicTestingSetup setup{ChainType::REGTEST};
    const CChainParams& params = Params();

    const auto msg = params.MessageStart();
    BOOST_REQUIRE_EQUAL(msg[0], 0x42);
    BOOST_REQUIRE_EQUAL(msg[1], 0x4e);
    BOOST_REQUIRE_EQUAL(msg[2], 0x47);
    BOOST_REQUIRE_EQUAL(msg[3], 0x03);

    BOOST_REQUIRE_EQUAL(params.GetDefaultPort(), 19444);
    BOOST_REQUIRE_EQUAL(params.Bech32HRP(), "bngr");

    // Regtest is local-only; policy may reuse upstream testnet/regtest prefix bytes.
    CheckBase58Prefix1(params, CChainParams::PUBKEY_ADDRESS, 111);
    CheckBase58Prefix1(params, CChainParams::SCRIPT_ADDRESS, 196);
    CheckBase58Prefix1(params, CChainParams::SECRET_KEY, 239);

    CheckBase58Prefix4(params, CChainParams::EXT_PUBLIC_KEY, {0x04, 0x35, 0x87, 0xCF});
    CheckBase58Prefix4(params, CChainParams::EXT_SECRET_KEY, {0x04, 0x35, 0x83, 0x94});

    BOOST_CHECK(GetNetworkForMagic(params.MessageStart()) == ChainType::REGTEST);
}

BOOST_AUTO_TEST_SUITE_END()
