// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CTAM_SHA256_H
#define BITCOIN_CRYPTO_CTAM_SHA256_H

#include <crypto/ctam_client.h>

#include <cstddef>
#include <cstdint>
#include <vector>

class CCTAMSHA256
{
private:
    CtamRestClient& m_client;
    CTAMDifficultyTriple m_difficulty;
    std::vector<unsigned char> m_buffer;

public:
    static constexpr std::size_t OUTPUT_SIZE{CtamRestClient::OUTPUT_SIZE};

    explicit CCTAMSHA256(CtamRestClient& client, CTAMDifficultyTriple difficulty);

    CCTAMSHA256& Write(const unsigned char* data, std::size_t len);
    void Finalize(unsigned char hash[OUTPUT_SIZE]);
    CCTAMSHA256& Reset();
};

#endif // BITCOIN_CRYPTO_CTAM_SHA256_H