// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/ctam_mining.h>

#include <crypto/common.h>
#include <primitives/block.h>
#include <streams.h>

#include <stdexcept>
#include <vector>

namespace {

std::vector<unsigned char> SerializeHeader(const CBlockHeader& header)
{
    DataStream stream;
    stream << header;
    std::vector<unsigned char> serialized(stream.size());
    std::copy(UCharCast(stream.data()), UCharCast(stream.data()) + stream.size(), serialized.begin());
    return serialized;
}

} // namespace

CtamHeaderMiningWork BuildCtamHeaderMiningWork(const CBlockHeader& header)
{
    CtamHeaderMiningWork work{};
    work.work_bytes = SerializeHeader(header);
    if (work.work_bytes.size() != BNG_HEADER_V1_SIZE) {
        throw std::runtime_error("CTAM header mining requires a serialized BNG header");
    }

    work.nonce_offset = BNG_HEADER_V1_NONCE_OFFSET;

    return work;
}
