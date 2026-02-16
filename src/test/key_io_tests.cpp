// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/key_io_invalid.json.h>
#include <test/data/key_io_valid.json.h>

#include <key.h>
#include <key_io.h>
#include <script/script.h>
#include <test/util/json.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>

BOOST_FIXTURE_TEST_SUITE(key_io_tests, BasicTestingSetup)

namespace {

void SwapCase(std::string& s)
{
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = (c - 'a') + 'A';
        } else if (c >= 'A' && c <= 'Z') {
            c = (c - 'A') + 'a';
        }
    }
}

} // namespace

// Goal: check that keys/addresses roundtrip and match canonical payload
BOOST_AUTO_TEST_CASE(key_io_valid_parse)
{
    UniValue tests = read_json(json_tests::key_io_valid);
    SelectParams(ChainType::MAIN);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) { // Allow for extra stuff (useful for comments)
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        const std::string exp_base58string = test[0].get_str();
        const std::vector<std::byte> exp_payload{ParseHex<std::byte>(test[1].get_str())};
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        bool try_case_flip = metadata.find_value("tryCaseFlip").isNull() ? false : metadata.find_value("tryCaseFlip").get_bool();
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            // Use payload as canonical source of truth; encode/decode under current chainparams.
            std::vector<unsigned char> key_bytes;
            key_bytes.reserve(exp_payload.size());
            for (const auto b : exp_payload) key_bytes.push_back(static_cast<unsigned char>(b));
            CKey key;
            key.Set(key_bytes.begin(), key_bytes.end(), isCompressed);
            BOOST_REQUIRE_MESSAGE(key.IsValid(), "Invalid key payload:" + strTest);

            const std::string encoded = EncodeSecret(key);
            const CKey decoded = DecodeSecret(encoded);
            BOOST_CHECK_MESSAGE(decoded.IsValid(), "DecodeSecret(EncodeSecret(key)) invalid:" + strTest);
            BOOST_CHECK_MESSAGE(decoded.IsCompressed() == isCompressed, "compressed mismatch:" + strTest);
            BOOST_CHECK_MESSAGE(decoded == key, "key roundtrip mismatch:" + strTest);

            // WIF must not decode as a destination.
            const auto destination = DecodeDestination(encoded);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid privkey as pubkey:" + strTest);

            (void)exp_base58string; // BNG intentionally diverges from upstream string encodings.
        } else {
            // Use payload as canonical source of truth; encode/decode under current chainparams.
            const std::vector<unsigned char> exp_payload_u8{ParseHex(test[1].get_str())};
            const CScript exp_script(exp_payload_u8.begin(), exp_payload_u8.end());

            CTxDestination dest;
            BOOST_REQUIRE_MESSAGE(ExtractDestination(exp_script, dest), "ExtractDestination failed:" + strTest);

            const std::string encoded = EncodeDestination(dest);
            const auto decoded_dest = DecodeDestination(encoded);
            BOOST_CHECK_MESSAGE(IsValidDestination(decoded_dest), "DecodeDestination(EncodeDestination(dest)) invalid:" + strTest);
            BOOST_CHECK_MESSAGE(decoded_dest == dest, "destination roundtrip mismatch:" + strTest);
            BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(decoded_dest)), HexStr(exp_payload_u8));

            // Try flipped case version on our encoded string.
            std::string flipped = encoded;
            SwapCase(flipped);
            const auto flipped_dest = DecodeDestination(flipped);
            BOOST_CHECK_MESSAGE(IsValidDestination(flipped_dest) == try_case_flip, "case-flip validity mismatch:" + strTest);
            if (IsValidDestination(flipped_dest)) {
                BOOST_CHECK_EQUAL(HexStr(GetScriptForDestination(flipped_dest)), HexStr(exp_payload_u8));
            }

            // Encoded address must not decode as a secret key.
            const auto privkey = DecodeSecret(encoded);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid pubkey as privkey:" + strTest);

            (void)exp_base58string; // BNG intentionally diverges from upstream string encodings.
        }
    }
}

// Goal: check that generated keys match test vectors
BOOST_AUTO_TEST_CASE(key_io_valid_gen)
{
    UniValue tests = read_json(json_tests::key_io_valid);

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 3) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        const std::string exp_base58string = test[0].get_str();
        std::vector<unsigned char> exp_payload = ParseHex(test[1].get_str());
        const UniValue &metadata = test[2].get_obj();
        bool isPrivkey = metadata.find_value("isPrivkey").get_bool();
        SelectParams(ChainTypeFromString(metadata.find_value("chain").get_str()).value());
        if (isPrivkey) {
            bool isCompressed = metadata.find_value("isCompressed").get_bool();
            CKey key;
            key.Set(exp_payload.begin(), exp_payload.end(), isCompressed);
            assert(key.IsValid());
            const std::string encoded = EncodeSecret(key);
            const CKey decoded = DecodeSecret(encoded);
            BOOST_CHECK_MESSAGE(decoded.IsValid(), "DecodeSecret(EncodeSecret(key)) invalid: " + strTest);
            BOOST_CHECK_MESSAGE(decoded == key, "secret key roundtrip mismatch: " + strTest);

            (void)exp_base58string; // BNG intentionally diverges from upstream string encodings.
        } else {
            CTxDestination dest;
            CScript exp_script(exp_payload.begin(), exp_payload.end());
            BOOST_CHECK(ExtractDestination(exp_script, dest));
            const std::string address = EncodeDestination(dest);
            const auto decoded_dest = DecodeDestination(address);
            BOOST_CHECK(IsValidDestination(decoded_dest));
            BOOST_CHECK(decoded_dest == dest);

            (void)exp_base58string; // BNG intentionally diverges from upstream string encodings.
        }
    }

    SelectParams(ChainType::MAIN);
}


// Goal: check that base58 parsing code is robust against a variety of corrupted data
BOOST_AUTO_TEST_CASE(key_io_invalid)
{
    UniValue tests = read_json(json_tests::key_io_invalid); // Negative testcases

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::string exp_base58string = test[0].get_str();

        // These vectors originate upstream; some strings can become valid under BNG's
        // different Base58/Bech32 version bytes. Only assert invalidity for entries that
        // are invalid under all supported chain params.
        bool valid_somewhere{false};
        for (const auto& chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST}) {
            SelectParams(chain);
            if (IsValidDestination(DecodeDestination(exp_base58string)) || DecodeSecret(exp_base58string).IsValid()) {
                valid_somewhere = true;
                break;
            }
        }
        if (valid_somewhere) {
            continue;
        }

        for (const auto& chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::SIGNET, ChainType::REGTEST}) {
            SelectParams(chain);
            const auto destination = DecodeDestination(exp_base58string);
            BOOST_CHECK_MESSAGE(!IsValidDestination(destination), "IsValid destination (unexpected) in chain=" + ChainTypeToString(chain) + ":" + strTest);
            const auto privkey = DecodeSecret(exp_base58string);
            BOOST_CHECK_MESSAGE(!privkey.IsValid(), "IsValid privkey (unexpected) in chain=" + ChainTypeToString(chain) + ":" + strTest);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
