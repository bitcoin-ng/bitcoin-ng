// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CTAM_CLIENT_H
#define BITCOIN_CRYPTO_CTAM_CLIENT_H

#include <span.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

class UniValue;

struct CTAMClientOptions {
    std::string base_url;
    int timeout_ms;
};

struct CTAMDifficultyTriple {
    static constexpr uint32_t MIN_MAJOR_CONTEXT_SIZE{27};
    static constexpr uint32_t DEFAULT_MAJOR_CONTEXT_SIZE{MIN_MAJOR_CONTEXT_SIZE};
    static constexpr uint8_t CONSENSUS_MINOR_ROUNDS{16};
    static constexpr uint8_t MAX_MINOR_ROUNDS{64};

    unsigned int energy_difficulty_bits;
    uint32_t major_context_size{DEFAULT_MAJOR_CONTEXT_SIZE};
    uint8_t minor_rounds{CONSENSUS_MINOR_ROUNDS};
};

class CtamRestClient
{
public:
    static constexpr std::size_t OUTPUT_SIZE{32};

    struct MineResult {
        bool found;
        std::optional<std::array<unsigned char, OUTPUT_SIZE>> digest;
        uint64_t branch_key;
        bool branch_kept;
        std::optional<uint32_t> winning_nonce;
        uint32_t nonce_start;
        uint32_t nonce_end;
        uint32_t nonce_offset;
        uint64_t attempts;
        unsigned int energy_difficulty_bits;
        uint32_t major_context_size;
        uint8_t minor_rounds;
    };

    struct MiningJob {
        std::string job_id;
        std::string status;
        bool found;
        std::optional<std::array<unsigned char, OUTPUT_SIZE>> digest;
        std::optional<uint32_t> winning_nonce;
        uint64_t attempts;
        uint32_t nonce_start;
        uint32_t nonce_end;
        uint32_t nonce_offset;
        uint64_t branch_key;
        bool branch_kept;
        unsigned int energy_difficulty_bits;
        uint32_t major_context_size;
        uint8_t minor_rounds;
        uint32_t injections;
        uint32_t rounds;
        std::optional<std::string> error;

        bool IsTerminal() const;
    };

    struct StatusResult {
        std::string ctam_version;
        std::string status;
        uint32_t context_size;
        uint64_t trunk_key;
        uint64_t branch_count;
    };

    struct CtaSha256Result {
        std::array<unsigned char, OUTPUT_SIZE> digest;
        uint64_t branch_key;
        bool branch_kept;
        uint32_t injections;
        uint32_t rounds;
    };

    struct CommitBranchResult {
        std::string status;
    };

    explicit CtamRestClient(const CTAMClientOptions& options);

    UniValue Health() const;
    StatusResult Status() const;
    std::array<unsigned char, OUTPUT_SIZE> CTA_SHA256(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch = false) const;
    CtaSha256Result CtaSha256Replay(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch = false) const;
    CtaSha256Result CtaSha256KeepBranch(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty) const;
    CommitBranchResult CommitBranch(uint64_t branch_key) const;
    StatusResult EnsureReadyForMining(const CTAMDifficultyTriple& difficulty) const;
    MiningJob StartMiningJob(std::span<const unsigned char> work_bytes,
                             uint32_t nonce_offset,
                             uint32_t nonce_start,
                             uint32_t nonce_end,
                             const CTAMDifficultyTriple& difficulty,
                             std::span<const unsigned char> initial_state = {},
                             bool keep_branch = false) const;
    MiningJob GetMiningJob(std::string_view job_id) const;
    MiningJob StopMiningJob(std::string_view job_id) const;
    MineResult MineCSHA256(std::span<const unsigned char> work_bytes,
                           uint32_t nonce_offset,
                           uint32_t nonce_start,
                           uint32_t nonce_end,
                           const CTAMDifficultyTriple& difficulty,
                           std::span<const unsigned char> initial_state = {},
                           bool keep_branch = false) const;

private:
    UniValue RequestJSON(int method, std::string_view path, std::string_view body = {}) const;

    std::string m_host;
    uint16_t m_port;
    std::string m_target_prefix;
    int m_timeout_ms;
};

#endif // BITCOIN_CRYPTO_CTAM_CLIENT_H
