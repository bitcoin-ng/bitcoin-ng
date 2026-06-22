// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_CTAM_MINER_H
#define BITCOIN_NODE_CTAM_MINER_H

#include <crypto/ctam_client.h>
#include <crypto/ctam_mining.h>
#include <uint256.h>

#include <cstdint>

class ArgsManager;
class CBlockHeader;
class ChainstateManager;

namespace util {
class SignalInterrupt;
} // namespace util

namespace node {

struct CtamMiningOptions {
    bool enabled{true};
    CTAMClientOptions client_options;
};

struct CtamNonceRange {
    uint32_t start{0};
    uint32_t end{0};
};

struct CtamSolvedHeader {
    uint32_t nonce{0};
    uint256 digest;
};

CtamMiningOptions GetCtamMiningOptions(const ArgsManager& args);
CTAMDifficultyTriple BuildCtamDifficulty(const CBlockHeader& header);

class CtamMiningBackend
{
public:
    explicit CtamMiningBackend(const ArgsManager& args);

    bool Enabled() const;
    uint32_t GetLiveContextSize() const;
    CTAMDifficultyTriple BuildDifficulty(const CBlockHeader& header) const;
    CtamHeaderMiningWork BuildHeaderWork(const CBlockHeader& header) const;

    bool SolveHeader(const util::SignalInterrupt& interrupt, CBlockHeader& header, uint64_t& max_tries, int expected_trunk_height, const uint256& expected_trunk_hash) const;
    bool SolveHeader(ChainstateManager& chainman, CBlockHeader& header, uint64_t& max_tries) const;
    bool VerifyHeader(const CBlockHeader& header, int expected_trunk_height, const uint256& expected_trunk_hash) const;

private:
    CtamMiningOptions m_options;
};

} // namespace node

#endif // BITCOIN_NODE_CTAM_MINER_H
