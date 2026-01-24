// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <chain.h>
#include <consensus/consensus.h>
#include <test/util/setup_common.h>
#include <util/chaintype.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(bng_regtest_init_tests)

// Treat chainparams as a black box: assert what regtest params *are*,
// then bootstrap the heavyweight fixture that tends to break first.
BOOST_AUTO_TEST_CASE(testchain100setup_regtest_bootstrap_smoke)
{
    SelectParams(ChainType::REGTEST);
    const CChainParams& params = Params();

    // GIP-0001 identity assertions (black box expectations).
    const auto msg = params.MessageStart();
    BOOST_REQUIRE_EQUAL(msg[0], 0x42);
    BOOST_REQUIRE_EQUAL(msg[1], 0x4e);
    BOOST_REQUIRE_EQUAL(msg[2], 0x47);
    BOOST_REQUIRE_EQUAL(msg[3], 0x03);
    BOOST_REQUIRE_EQUAL(params.GetDefaultPort(), 19444);
    BOOST_REQUIRE_EQUAL(params.Bech32HRP(), "bngr");

    // Common cause: genesis timestamp too far in the future for header time checks.
    const int64_t now = GetTime();
    const int64_t genesis_time = static_cast<int64_t>(params.GenesisBlock().nTime);
    BOOST_REQUIRE_MESSAGE(
        genesis_time <= now + MAX_FUTURE_BLOCK_TIME,
        "regtest genesis nTime is too far in the future for chain init.\n"
        "genesis.nTime=" << genesis_time << " now=" << now
                         << " max_future=" << (now + MAX_FUTURE_BLOCK_TIME));

    // The “break surface”: full chainstate init + genesis handling.
    try {
        TestChain100Setup setup{ChainType::REGTEST};
        (void)setup;
        BOOST_CHECK(true);
    } catch (const std::exception& e) {
        BOOST_FAIL(std::string{
            "TestChain100Setup(regtest) threw. This fixture does full chainstate init "
            "(including genesis acceptance), so it will fail on genesis/consensus/init issues.\n"
            "Exception: "} + e.what());
    }
}

BOOST_AUTO_TEST_SUITE_END()