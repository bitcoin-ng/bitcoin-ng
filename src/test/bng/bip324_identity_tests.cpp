// Copyright (c) 2026-present The Bitcoin-NG developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bip324.h>
#include <chainparams.h>
#include <key.h>
#include <kernel/messagestartchars.h>
#include <pubkey.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace {

constexpr MessageStartChars BITCOIN_MAINNET_MESSAGE_START{{0xF9, 0xBE, 0xB4, 0xD9}};

struct Vector1Inputs {
    std::vector<unsigned char> priv_ours;
    std::vector<std::byte> ellswift_ours;
    std::vector<std::byte> ellswift_theirs;
    bool initiating;
};

Vector1Inputs GetVector1()
{
    // Copied from the first packet_test_vectors entry in src/test/bip324_tests.cpp.
    Vector1Inputs in;
    in.priv_ours = ParseHex("61062ea5071d800bbfd59e2e8b53d47d194b095ae5a4df04936b49772ef0d4d7");
    in.ellswift_ours = ParseHex<std::byte>("ec0adff257bbfe500c188c80b4fdd640f6b45a482bbc15fc7cef5931deff0aa186f6eb9bba7b85dc4dcc28b28722de1e3d9108b985e2967045668f66098e475b");
    in.ellswift_theirs = ParseHex<std::byte>("a4a94dfce69b4a2a0a099313d10f9f7e7d649d60501c9e1d274c300e0d89aafaffffffffffffffffffffffffffffffffffffffffffffffffffffffff8faf88d5");
    in.initiating = true;
    return in;
}

} // namespace

BOOST_AUTO_TEST_SUITE(bng_bip324_identity_tests)

BOOST_AUTO_TEST_CASE(vector1_bng_magic_pins_outputs)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const auto in = GetVector1();

    // Expected values for BNG mainnet (derived using Params().MessageStart() as part of HKDF salt).
    const std::vector<std::byte> expected_session_id = ParseHex<std::byte>(
        "28dcb383335a98a92f5f788a3884d2d81bf6cf8a6aa7574971fb70cd3f8c16ce");
    const std::vector<std::byte> expected_send_garbage = ParseHex<std::byte>(
        "d474ca89d80c07fda7fb5cf4df14bb9e");
    const std::vector<std::byte> expected_recv_garbage = ParseHex<std::byte>(
        "47699a23a5aa6d80a8375635945d7fa5");

    CKey key;
    key.Set(in.priv_ours.begin(), in.priv_ours.end(), true);
    EllSwiftPubKey ours(in.ellswift_ours);
    EllSwiftPubKey theirs(in.ellswift_theirs);

    BIP324Cipher cipher(key, ours);
    cipher.Initialize(theirs, in.initiating);

    BOOST_CHECK(std::ranges::equal(expected_session_id, cipher.GetSessionID()));
    BOOST_CHECK(std::ranges::equal(expected_send_garbage, cipher.GetSendGarbageTerminator()));
    BOOST_CHECK(std::ranges::equal(expected_recv_garbage, cipher.GetReceiveGarbageTerminator()));
}

BOOST_AUTO_TEST_CASE(bitcoin_override_differs_from_bng)
{
    BasicTestingSetup setup{ChainType::MAIN};

    const auto in = GetVector1();

    CKey key;
    key.Set(in.priv_ours.begin(), in.priv_ours.end(), true);
    EllSwiftPubKey ours(in.ellswift_ours);
    EllSwiftPubKey theirs(in.ellswift_theirs);

    BIP324Cipher bng_cipher(key, ours);
    bng_cipher.Initialize(theirs, in.initiating);

    BIP324Cipher btc_cipher(key, ours);
    btc_cipher.Initialize(theirs, in.initiating, /*self_decrypt=*/false, BITCOIN_MAINNET_MESSAGE_START);

    // For GIP-0001, BNG mainnet message start must not equal Bitcoin mainnet magic.
    BOOST_CHECK(Params().MessageStart() != BITCOIN_MAINNET_MESSAGE_START);

    // And because message start is in HKDF salt, derived session_id should differ.
    BOOST_CHECK(!std::ranges::equal(bng_cipher.GetSessionID(), btc_cipher.GetSessionID()));
}

BOOST_AUTO_TEST_SUITE_END()
