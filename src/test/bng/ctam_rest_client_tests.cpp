// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <common/system.h>
#include <crypto/ctam_client.h>
#include <crypto/ctam_mining.h>
#include <crypto/ctam_sha256.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr unsigned int DEFAULT_ENERGY_DIFFICULTY_BITS{0x1d00ffff};
constexpr unsigned int EASY_ENERGY_DIFFICULTY_BITS{0x207fffff};
constexpr int DEFAULT_CTAM_TIMEOUT_MS{5000};

bool CTAMTestsEnabled()
{
    return gArgs.GetBoolArg("-ctam", false);
}

CTAMClientOptions GetCTAMClientOptions()
{
    return {
        .base_url = gArgs.GetArg("-ctamresturl", "http://127.0.0.1:8080/"),
        .timeout_ms = static_cast<int>(gArgs.GetIntArg("-ctamtimeoutms", DEFAULT_CTAM_TIMEOUT_MS)),
    };
}

CTAMDifficultyTriple GetTestDifficulty(uint32_t context_size)
{
    return {
        .energy_difficulty_bits = DEFAULT_ENERGY_DIFFICULTY_BITS,
        .major_context_size = context_size,
        .minor_rounds = CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS,
    };
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(ctam_rest_client_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(csha256_smoke)
{
    if (!CTAMTestsEnabled()) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: rerun with -- -ctam=1 and a local CTAM daemon");
        return;
    }

    CtamRestClient client{GetCTAMClientOptions()};

    const UniValue health = client.Health();
    BOOST_REQUIRE_MESSAGE(health.exists("test") && health["test"].isStr(), "CTAM health response is missing the test field");
    BOOST_CHECK_EQUAL(health["test"].get_str(), "ok");
    BOOST_REQUIRE_MESSAGE(health.exists("context_size") && health["context_size"].isNum(), "CTAM health response is missing the context_size field");
    const auto context_size = static_cast<uint32_t>(health["context_size"].getInt<int64_t>());
    if (context_size < CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: local CTAM context_size " + std::to_string(context_size) +
                           " is below the BNG minimum of " + std::to_string(CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) +
                           " (~16 MiB)");
        return;
    }

    const std::string message{"ctam csha256 smoke"};
    const auto difficulty = GetTestDifficulty(context_size);

    const auto direct_digest = client.CTA_SHA256(MakeUCharSpan(message), difficulty);
    BOOST_CHECK(std::any_of(direct_digest.begin(), direct_digest.end(), [](unsigned char byte) { return byte != 0; }));

    CCTAMSHA256 csha256{client, difficulty};
    std::array<unsigned char, CCTAMSHA256::OUTPUT_SIZE> wrapped_digest{};
    csha256.Write(reinterpret_cast<const unsigned char*>(message.data()), message.size());
    csha256.Finalize(wrapped_digest.data());

    BOOST_CHECK(std::equal(direct_digest.begin(), direct_digest.end(), wrapped_digest.begin()));
}

BOOST_AUTO_TEST_CASE(csha256_mining_smoke)
{
    if (!CTAMTestsEnabled()) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: rerun with -- -ctam=1 and a local CTAM daemon");
        return;
    }

    CtamRestClient client{GetCTAMClientOptions()};

    const UniValue health = client.Health();
    BOOST_REQUIRE_MESSAGE(health.exists("context_size") && health["context_size"].isNum(), "CTAM health response is missing the context_size field");
    const auto context_size = static_cast<uint32_t>(health["context_size"].getInt<int64_t>());
    if (context_size < CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: local CTAM context_size " + std::to_string(context_size) +
                           " is below the BNG minimum of " + std::to_string(CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) +
                           " (~16 MiB)");
        return;
    }

    const CTAMDifficultyTriple difficulty{
        .energy_difficulty_bits = EASY_ENERGY_DIFFICULTY_BITS,
        .major_context_size = context_size,
        .minor_rounds = CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS,
    };

    std::array<unsigned char, 16> work_bytes{};
    const auto result = client.MineCSHA256(work_bytes, /*nonce_offset=*/0, /*nonce_start=*/0, /*nonce_end=*/4095, difficulty);

    BOOST_REQUIRE(result.found);
    BOOST_REQUIRE(result.digest.has_value());
    BOOST_REQUIRE(result.winning_nonce.has_value());
    BOOST_CHECK_GE(*result.winning_nonce, 0U);
    BOOST_CHECK_LE(*result.winning_nonce, 4095U);
    BOOST_CHECK_EQUAL(result.nonce_start, 0U);
    BOOST_CHECK_EQUAL(result.nonce_end, 4095U);
    BOOST_CHECK_EQUAL(result.nonce_offset, 0U);
    BOOST_CHECK_GT(result.attempts, 0U);
    BOOST_CHECK_LE(result.attempts, 4096U);
    BOOST_CHECK(std::any_of(result.digest->begin(), result.digest->end(), [](unsigned char byte) { return byte != 0; }));
}

BOOST_AUTO_TEST_CASE(csha256_header_mining_smoke)
{
    if (!CTAMTestsEnabled()) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: rerun with -- -ctam=1 and a local CTAM daemon");
        return;
    }

    CtamRestClient client{GetCTAMClientOptions()};

    const UniValue health = client.Health();
    BOOST_REQUIRE_MESSAGE(health.exists("context_size") && health["context_size"].isNum(), "CTAM health response is missing the context_size field");
    const auto context_size = static_cast<uint32_t>(health["context_size"].getInt<int64_t>());
    if (context_size < CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) {
        BOOST_TEST_MESSAGE("skipping ctam_rest_client_tests: local CTAM context_size " + std::to_string(context_size) +
                           " is below the BNG minimum of " + std::to_string(CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) +
                           " (~16 MiB)");
        return;
    }

    const CTAMDifficultyTriple difficulty{
        .energy_difficulty_bits = EASY_ENERGY_DIFFICULTY_BITS,
        .major_context_size = context_size,
        .minor_rounds = CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS,
    };

    CBlockHeader header;
    header.nVersion = 4;
    header.nTime = 1'700'000'000;
    header.nBits = difficulty.energy_difficulty_bits;
    header.nNonce = 0;

    const CtamHeaderMiningWork work = BuildCtamHeaderMiningWork(header);
    BOOST_CHECK_EQUAL(work.work_bytes.size(), 80U);
    BOOST_CHECK_EQUAL(work.nonce_offset, 76U);

    const auto result = client.MineCSHA256(work.work_bytes,
                                           work.nonce_offset,
                                           /*nonce_start=*/0,
                                           /*nonce_end=*/4095,
                                           difficulty);

    BOOST_REQUIRE(result.found);
    BOOST_REQUIRE(result.digest.has_value());
    BOOST_REQUIRE(result.winning_nonce.has_value());
    BOOST_CHECK_LE(*result.winning_nonce, 4095U);
    BOOST_CHECK_EQUAL(result.nonce_offset, 76U);
    BOOST_CHECK_GT(result.attempts, 0U);
    BOOST_CHECK(std::any_of(result.digest->begin(), result.digest->end(), [](unsigned char byte) { return byte != 0; }));
}

BOOST_AUTO_TEST_SUITE_END()