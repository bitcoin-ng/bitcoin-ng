// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/ctam_sha256.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace {

void ValidateDifficultyTriple(const CTAMDifficultyTriple& difficulty)
{
    if (difficulty.energy_difficulty_bits == 0) {
        throw std::runtime_error("CTAM energy difficulty bits must be non-zero");
    }
    if (difficulty.minor_rounds != CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS) {
        throw std::runtime_error("CTAM minor difficulty must match the consensus round count");
    }
}

} // namespace

CCTAMSHA256::CCTAMSHA256(CtamRestClient& client, CTAMDifficultyTriple difficulty)
    : m_client{client},
      m_difficulty{difficulty}
{
    ValidateDifficultyTriple(m_difficulty);
}

CCTAMSHA256& CCTAMSHA256::Write(const unsigned char* data, std::size_t len)
{
    if (len == 0) {
        return *this;
    }

    m_buffer.insert(m_buffer.end(), data, data + len);
    return *this;
}

void CCTAMSHA256::Finalize(unsigned char hash[OUTPUT_SIZE])
{
    const auto digest = m_client.CTA_SHA256(m_buffer, m_difficulty);
    std::copy(digest.begin(), digest.end(), hash);
}

CCTAMSHA256& CCTAMSHA256::Reset()
{
    m_buffer.clear();
    return *this;
}