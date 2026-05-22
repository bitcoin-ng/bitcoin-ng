// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <common/system.h>
#include <crypto/ctam_client.h>
#include <crypto/ctam_sha256.h>
#include <test/util/setup_common.h>
#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace {

constexpr unsigned int DEFAULT_ENERGY_DIFFICULTY_BITS{0x1d00ffff};
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

BOOST_AUTO_TEST_SUITE_END()