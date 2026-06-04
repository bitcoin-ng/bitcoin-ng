// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CTAM_MINING_H
#define BITCOIN_CRYPTO_CTAM_MINING_H

#include <span.h>

#include <cstdint>
#include <vector>

class CBlockHeader;

struct CtamHeaderMiningWork {
    std::vector<unsigned char> work_bytes;
    uint32_t nonce_offset;
};

CtamHeaderMiningWork BuildCtamHeaderMiningWork(const CBlockHeader& header);

#endif // BITCOIN_CRYPTO_CTAM_MINING_H