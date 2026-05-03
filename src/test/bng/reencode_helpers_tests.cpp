// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/bng/reencode_helpers.h>

#include <addresstype.h>
#include <base58.h>
#include <bech32.h>
#include <chainparams.h>
#include <key.h>
#include <key_io.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace util::hex_literals;

BOOST_AUTO_TEST_SUITE(bng_reencode_helpers_tests)

namespace {

// Bitcoin mainnet WIF for an *uncompressed* private key (prefix 0x80).
// Used as a stable container for 32 key bytes.
constexpr const char* BITCOIN_MAINNET_WIF_UNCOMPRESSED =
    "5Kg1gnAjaLfKiwhhPpGS3QfRg2m6awQvaj98JCZBZQ5SuS2F15C";

// BIP32 test vector 1 (Bitcoin mainnet xpub/xprv version bytes).
constexpr const char* BITCOIN_XPRV_TV1 =
    "xprv9s21ZrQH143K3QTDL4LXw2F7HEK3wJUD2nW2nRk4stbPy6cq3jPPqjiChkVvvNKmPGJxWUtg6LnF5kejMRNNU3TGtRBeJgk33yuGBxrMPHi";
constexpr const char* BITCOIN_XPUB_TV1 =
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8";

std::vector<unsigned char> DecodeBase58CheckRaw(const std::string& s, const size_t max_len)
{
    std::vector<unsigned char> out;
    BOOST_REQUIRE(DecodeBase58Check(s, out, max_len));
    return out;
}

CKey KeyFromWifPayloadBytes(const std::string& wif)
{
    const auto decoded = DecodeBase58CheckRaw(wif, 34);
    BOOST_REQUIRE(decoded.size() == 33U || (decoded.size() == 34U && decoded.back() == 1));

    const bool compressed = decoded.size() == 34U;
    CKey key;
    key.Set(decoded.begin() + 1, decoded.begin() + 33, compressed);
    BOOST_REQUIRE(key.IsValid());
    return key;
}

} // namespace

BOOST_AUTO_TEST_CASE(reencode_wif_mainnet)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const CKey expected = KeyFromWifPayloadBytes(BITCOIN_MAINNET_WIF_UNCOMPRESSED);

    const auto rewritten = bng::test::ReencodeBase58CheckKeyToken(BITCOIN_MAINNET_WIF_UNCOMPRESSED);
    BOOST_REQUIRE(rewritten);

    // Must decode under current (BNG) network.
    const CKey decoded = DecodeSecret(*rewritten);
    BOOST_REQUIRE(decoded.IsValid());
    BOOST_CHECK(decoded == expected);

    // Prefix byte must match current chain params.
    const auto raw = DecodeBase58CheckRaw(*rewritten, 34);
    BOOST_REQUIRE(!Params().Base58Prefix(CChainParams::SECRET_KEY).empty());
    BOOST_CHECK_EQUAL(raw[0], Params().Base58Prefix(CChainParams::SECRET_KEY)[0]);
}

BOOST_AUTO_TEST_CASE(reencode_wif_testnet)
{
    BasicTestingSetup setup{ChainType::TESTNET};

    const auto rewritten = bng::test::ReencodeBase58CheckKeyToken(BITCOIN_MAINNET_WIF_UNCOMPRESSED);
    BOOST_REQUIRE(rewritten);

    const auto raw = DecodeBase58CheckRaw(*rewritten, 34);
    BOOST_REQUIRE(!Params().Base58Prefix(CChainParams::SECRET_KEY).empty());
    BOOST_CHECK_EQUAL(raw[0], Params().Base58Prefix(CChainParams::SECRET_KEY)[0]);
    BOOST_CHECK(DecodeSecret(*rewritten).IsValid());
}

BOOST_AUTO_TEST_CASE(reencode_bip32_ext_keys_mainnet)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const auto xprv_rewritten = bng::test::ReencodeBase58CheckKeyToken(BITCOIN_XPRV_TV1);
    const auto xpub_rewritten = bng::test::ReencodeBase58CheckKeyToken(BITCOIN_XPUB_TV1);
    BOOST_REQUIRE(xprv_rewritten);
    BOOST_REQUIRE(xpub_rewritten);

    // Version bytes must match current chain params.
    const auto raw_prv = DecodeBase58CheckRaw(*xprv_rewritten, 78);
    const auto raw_pub = DecodeBase58CheckRaw(*xpub_rewritten, 78);

    const auto& prv_prefix = Params().Base58Prefix(CChainParams::EXT_SECRET_KEY);
    const auto& pub_prefix = Params().Base58Prefix(CChainParams::EXT_PUBLIC_KEY);
    BOOST_REQUIRE_EQUAL(prv_prefix.size(), 4U);
    BOOST_REQUIRE_EQUAL(pub_prefix.size(), 4U);

    BOOST_CHECK(std::equal(prv_prefix.begin(), prv_prefix.end(), raw_prv.begin()));
    BOOST_CHECK(std::equal(pub_prefix.begin(), pub_prefix.end(), raw_pub.begin()));

    // Must decode under current (BNG) network.
    BOOST_CHECK(DecodeExtKey(*xprv_rewritten).key.IsValid());
    BOOST_CHECK(DecodeExtPubKey(*xpub_rewritten).pubkey.IsValid());
}

BOOST_AUTO_TEST_CASE(reencode_descriptor_strings_checksum_behavior)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const std::string desc_no_checksum = std::string{"pkh("} + BITCOIN_MAINNET_WIF_UNCOMPRESSED + ")";
    const std::string rewritten = bng::test::ReencodeWifKeys(desc_no_checksum);
    BOOST_CHECK(rewritten.find(BITCOIN_MAINNET_WIF_UNCOMPRESSED) == std::string::npos);

    const std::string desc_with_checksum = desc_no_checksum + "#deadbeef";
    BOOST_CHECK_EQUAL(bng::test::ReencodeWifKeys(desc_with_checksum), desc_with_checksum);

    const std::string fixed_for_parsing = bng::test::FixupDescriptorForParsing(desc_with_checksum);
    BOOST_CHECK(fixed_for_parsing.find('#') == std::string::npos);
    BOOST_CHECK(fixed_for_parsing.find(BITCOIN_MAINNET_WIF_UNCOMPRESSED) == std::string::npos);

    const std::string preserved = bng::test::ReencodeWifKeysPreserveChecksum(desc_with_checksum);
    BOOST_CHECK(preserved.ends_with("#deadbeef"));
    BOOST_CHECK(preserved.find(BITCOIN_MAINNET_WIF_UNCOMPRESSED) == std::string::npos);
}

BOOST_AUTO_TEST_CASE(reencode_addresses_base58_and_bech32)
{
    BasicTestingSetup setup{ChainType::MAIN};

    // Base58 P2PKH (Bitcoin mainnet prefix 0x00) for a stable pubkey-hash.
    const std::string btc_p2pkh = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";

    const auto rewritten_base58 = bng::test::ReencodeAddress(btc_p2pkh);
    BOOST_REQUIRE(rewritten_base58);

    // Hash160 payload must remain the same.
    const auto raw_btc = DecodeBase58CheckRaw(btc_p2pkh, 100);
    BOOST_REQUIRE_EQUAL(raw_btc.size(), 21U);
    uint160 expected_hash;
    std::copy(raw_btc.begin() + 1, raw_btc.end(), expected_hash.begin());

    const CTxDestination dst = DecodeDestination(*rewritten_base58);
    const PKHash* pkhash = std::get_if<PKHash>(&dst);
    BOOST_REQUIRE(pkhash);
    BOOST_CHECK(*pkhash == PKHash(expected_hash));

    // Construct a valid Bitcoin-mainnet Bech32 v0 program and encode under HRP "bc".
    // (We don't rely on a hard-coded string to avoid accidental checksum issues.)
    constexpr std::array<uint8_t, 20> witness_program{"000102030405060708090a0b0c0d0e0f10111213"_hex_u8};
    std::vector<unsigned char> btc_data = {0}; // witness version 0
    btc_data.reserve(1 + witness_program.size() * 8 / 5);
    ConvertBits<8, 5, true>([&](unsigned char c) { btc_data.push_back(c); }, witness_program.begin(), witness_program.end());
    const std::string btc_bech32 = bech32::Encode(bech32::Encoding::BECH32, "bc", btc_data);
    const auto rewritten_bech32 = bng::test::ReencodeAddress(btc_bech32);
    BOOST_REQUIRE(rewritten_bech32);
    BOOST_CHECK(rewritten_bech32->starts_with(Params().Bech32HRP() + "1"));

    const CTxDestination dst2 = DecodeDestination(*rewritten_bech32);
    const WitnessV0KeyHash* w0 = std::get_if<WitnessV0KeyHash>(&dst2);
    BOOST_REQUIRE(w0);
    BOOST_CHECK(std::equal(witness_program.begin(), witness_program.end(), w0->begin()));

    // addr(...) wrapper helper.
    const std::string addr_desc = "addr(" + btc_p2pkh + ")";
    const std::string addr_desc_rewritten = bng::test::FixupAddrDescriptorForExpectedOutput(addr_desc);
    BOOST_CHECK(addr_desc_rewritten.starts_with("addr("));
    BOOST_CHECK(addr_desc_rewritten.ends_with(")"));
    BOOST_CHECK(addr_desc_rewritten != addr_desc);
}

BOOST_AUTO_TEST_SUITE_END()
