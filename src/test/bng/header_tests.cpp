// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>

namespace {

CBlockHeader Header()
{
    CBlockHeader header;
    header.nVersion = 4;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256{2};
    header.hashHistoryMMRRoot = uint256{3};
    header.nTime = 1'700'000'000;
    header.nBits = 0x207fffff;
    header.nMajorWorkCtxSize = BNG_MAJOR_WORK_CTX_SIZE_DEFAULT;
    header.nMinorWorkRounds = BNG_MINOR_WORK_ROUNDS_FIXED;
    header.nNonce = 0x11223344;
    return header;
}

DataStream SerializeHeader(const CBlockHeader& header)
{
    DataStream stream;
    stream << header;
    return stream;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(bng_header_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(serialized_layout)
{
    const CBlockHeader header = Header();
    const DataStream stream = SerializeHeader(header);

    BOOST_CHECK_EQUAL(stream.size(), BNG_HEADER_V1_SIZE);
    BOOST_CHECK_EQUAL(::GetSerializeSize(header), BNG_HEADER_V1_SIZE);
    BOOST_CHECK_EQUAL(BNG_HEADER_V1_NONCE_OFFSET, 111U);

    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[108])), BNG_MAJOR_WORK_CTX_SIZE_DEFAULT);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[109])), 0U);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[110])), BNG_MINOR_WORK_ROUNDS_FIXED);

    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[BNG_HEADER_V1_NONCE_OFFSET])), 0x44U);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[BNG_HEADER_V1_NONCE_OFFSET + 1])), 0x33U);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[BNG_HEADER_V1_NONCE_OFFSET + 2])), 0x22U);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(static_cast<unsigned char>(stream[BNG_HEADER_V1_NONCE_OFFSET + 3])), 0x11U);
}

BOOST_AUTO_TEST_CASE(hash_uses_extended_fields)
{
    const CBlockHeader base = Header();

    CBlockHeader changed_history = base;
    changed_history.hashHistoryMMRRoot = uint256{4};
    BOOST_CHECK(base.GetHash() != changed_history.GetHash());

    CBlockHeader changed_major = base;
    changed_major.nMajorWorkCtxSize += 1;
    BOOST_CHECK(base.GetHash() != changed_major.GetHash());

    CBlockHeader changed_minor = base;
    changed_minor.nMinorWorkRounds += 1;
    BOOST_CHECK(base.GetHash() != changed_minor.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()
