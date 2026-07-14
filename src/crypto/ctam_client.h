// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CTAM_CLIENT_H
#define BITCOIN_CRYPTO_CTAM_CLIENT_H

#include <span.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
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

    unsigned int energy_difficulty;
    uint32_t major_ctam_difficulty_context_size{DEFAULT_MAJOR_CONTEXT_SIZE};
    uint8_t minor_ctam_difficulty_rounds{CONSENSUS_MINOR_ROUNDS};
};

class CtamRestError : public std::runtime_error
{
public:
    CtamRestError(std::string message, int error);

    int ErrorCode() const { return m_error; }
    bool IsTimeout() const;

private:
    int m_error;
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
        unsigned int energy_difficulty;
        uint32_t major_ctam_difficulty_context_size;
        uint8_t minor_ctam_difficulty_rounds;
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
        unsigned int energy_difficulty;
        uint32_t major_ctam_difficulty_context_size;
        uint8_t minor_ctam_difficulty_rounds;
        uint32_t injection_rounds_difficulty;
        uint32_t rounds;
        std::optional<std::string> error;

        bool IsTerminal() const;
    };

    struct StatusResult {
        std::string ctam_version;
        std::string status;
        uint32_t context_size;
        uint64_t trunk_key;
        std::optional<uint64_t> bng_height;
        std::optional<std::string> bng_block_hash;
        uint64_t branch_count;
    };

    struct CtaSha256Result {
        std::array<unsigned char, OUTPUT_SIZE> digest;
        uint64_t branch_key;
        bool branch_kept;
        uint32_t injection_rounds_difficulty;
        uint32_t rounds;
    };

    struct CommitBranchResult {
        std::string status;
    };

    struct ResetResult {
        std::string status;
        uint32_t context_size;
        uint64_t trunk_key;
        std::optional<uint64_t> bng_height;
        std::optional<std::string> bng_block_hash;
        uint64_t branch_count;
    };

    struct RewindResult {
        std::string status;
        uint64_t old_height;
        std::string old_block_hash;
        uint64_t new_height;
        std::string new_block_hash;
        uint64_t trunk_key;
        uint64_t applied_entries;
        uint64_t num_of_commits;
        uint64_t remaining_rewind_depth;
    };

    explicit CtamRestClient(const CTAMClientOptions& options);
    ~CtamRestClient();

    UniValue Health() const;
    StatusResult Status() const;
    std::array<unsigned char, OUTPUT_SIZE> CTA_SHA256(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch = false) const;
    CtaSha256Result PostCtaSha256Message(std::span<const unsigned char> input, const CTAMDifficultyTriple& difficulty, bool keep_branch = false) const;
    CtaSha256Result PostCtaSha256MessageKeepBranch(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty) const;
    CommitBranchResult CommitBranch(uint64_t branch_key) const;
    CommitBranchResult CommitBranch(uint64_t branch_key, uint64_t bng_height, std::string_view bng_block_hash) const;
    ResetResult AdminReset(uint32_t context_size, std::optional<uint64_t> bng_height = std::nullopt, std::optional<std::string_view> bng_block_hash = std::nullopt) const;
    RewindResult Rewind(uint64_t tip_height, std::string_view tip_block_hash,
                        uint64_t back_to_height, std::string_view back_to_block_hash,
                        std::optional<uint64_t> num_of_commits = std::nullopt) const;
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
    //! Reusable libevent transport state (event base + persistent HTTP/1.1
    //! connection). Created lazily, discarded whenever connection reuse would
    //! be unsafe, and recreated on the next request.
    struct Transport;

    UniValue RequestJSON(int method, std::string_view path, std::string_view body = {}) const;
    //! Perform one request attempt on the persistent transport. Requires
    //! m_transport_mutex to be held by the caller.
    UniValue RequestOnce(int method, const std::string& target, std::string_view body) const;
    Transport& EnsureTransport() const;
    void ResetTransport() const;

    std::string m_host;
    uint16_t m_port;
    std::string m_target_prefix;
    int m_timeout_ms;
    //! Serializes requests: one HTTP/1.1 connection must not carry
    //! interleaved requests from concurrent callers.
    mutable std::mutex m_transport_mutex;
    mutable std::unique_ptr<Transport> m_transport;
};

#endif // BITCOIN_CRYPTO_CTAM_CLIENT_H
