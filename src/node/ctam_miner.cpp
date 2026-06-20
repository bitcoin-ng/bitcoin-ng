// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/ctam_miner.h>

#include <chain.h>
#include <common/args.h>
#include <logging.h>
#include <util/signalinterrupt.h>
#include <validation.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>

namespace node {
namespace {

constexpr int DEFAULT_CTAM_TIMEOUT_MS{5000};
constexpr auto CTAM_MINING_POLL_INTERVAL{std::chrono::milliseconds{50}};

std::array<unsigned char, CtamRestClient::OUTPUT_SIZE> CompactTargetFromBits(unsigned int bits)
{
    const unsigned int exponent = (bits >> 24) & 0xff;
    const unsigned int mantissa = bits & 0x007fffff;
    const bool negative = (bits & 0x00800000) != 0;
    if (negative || mantissa == 0 || exponent == 0 || exponent > CtamRestClient::OUTPUT_SIZE) {
        throw std::runtime_error("CTAM energy difficulty encodes an invalid target");
    }

    std::array<unsigned char, CtamRestClient::OUTPUT_SIZE> target{};
    if (exponent <= 3) {
        const unsigned int value = mantissa >> (8 * (3 - exponent));
        const unsigned int start = CtamRestClient::OUTPUT_SIZE - exponent;
        for (unsigned int i = 0; i < exponent; ++i) {
            target[start + i] = static_cast<unsigned char>((value >> (8 * (exponent - 1 - i))) & 0xff);
        }
    } else {
        const unsigned int start = CtamRestClient::OUTPUT_SIZE - exponent;
        target[start] = static_cast<unsigned char>((mantissa >> 16) & 0xff);
        target[start + 1] = static_cast<unsigned char>((mantissa >> 8) & 0xff);
        target[start + 2] = static_cast<unsigned char>(mantissa & 0xff);
    }
    return target;
}

bool DigestMeetsTarget(const std::array<unsigned char, CtamRestClient::OUTPUT_SIZE>& digest, unsigned int bits)
{
    const auto target = CompactTargetFromBits(bits);
    return digest <= target;
}

bool IsCtamZeroBlockState(const CtamRestClient::StatusResult& status)
{
    return status.trunk_key == 0 && !status.bng_height && !status.bng_block_hash;
}

void RequireCtamTrunkState(const CtamRestClient::StatusResult& status, int expected_height, const uint256& expected_hash, uint32_t expected_context_size)
{
    if (status.status != "running") {
        throw std::runtime_error("CTAM status is not running");
    }
    if (status.context_size != expected_context_size) {
        throw std::runtime_error(strprintf("CTAM context_size %u does not match expected context_size %u", status.context_size, expected_context_size));
    }
    if (expected_height < 0) {
        throw std::runtime_error("CTAM trunk state requires a non-negative BNG height");
    }
    if (expected_height == 0 && IsCtamZeroBlockState(status)) {
        return;
    }
    if (!status.bng_height) {
        throw std::runtime_error("CTAM status is missing bng_height; cannot prove trunk state matches BNG chain prefix");
    }
    if (*status.bng_height != static_cast<uint64_t>(expected_height)) {
        throw std::runtime_error(strprintf("CTAM trunk height %u does not match expected BNG height %d", *status.bng_height, expected_height));
    }
    if (!status.bng_block_hash) {
        throw std::runtime_error("CTAM status is missing bng_block_hash; cannot prove trunk state matches BNG chain prefix");
    }
    if (*status.bng_block_hash != expected_hash.ToString()) {
        throw std::runtime_error("CTAM trunk block hash does not match expected BNG chain prefix");
    }
}

bool CtamTrunkMatchesBlock(const CtamRestClient::StatusResult& status, int height, const uint256& hash, uint32_t expected_context_size)
{
    if (status.status != "running" || status.context_size != expected_context_size || height < 0) {
        return false;
    }
    return status.bng_height && *status.bng_height == static_cast<uint64_t>(height) &&
           status.bng_block_hash && *status.bng_block_hash == hash.ToString();
}

} // namespace

CtamMiningOptions GetCtamMiningOptions(const ArgsManager& args)
{
    return {
        .enabled = args.GetBoolArg("-ctammining", true),
        .client_options = {
            .base_url = args.GetArg("-ctamresturl", "http://127.0.0.1:8080/"),
            .timeout_ms = static_cast<int>(args.GetIntArg("-ctamtimeoutms", DEFAULT_CTAM_TIMEOUT_MS)),
        },
    };
}

CTAMDifficultyTriple BuildCtamDifficulty(const CBlockHeader& header, uint32_t context_size)
{
    return {
        .energy_difficulty = header.nBits,
        .major_ctam_difficulty_context_size = context_size,
        .minor_ctam_difficulty_rounds = CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS,
    };
}

CtamMiningBackend::CtamMiningBackend(const ArgsManager& args)
    : m_options{GetCtamMiningOptions(args)}
{
}

bool CtamMiningBackend::Enabled() const
{
    return m_options.enabled;
}

uint32_t CtamMiningBackend::GetLiveContextSize() const
{
    CtamRestClient client{m_options.client_options};
    return client.Status().context_size;
}

CTAMDifficultyTriple CtamMiningBackend::BuildDifficulty(const CBlockHeader& header) const
{
    return BuildCtamDifficulty(header, GetLiveContextSize());
}

CtamHeaderMiningWork CtamMiningBackend::BuildHeaderWork(const CBlockHeader& header) const
{
    return BuildCtamHeaderMiningWork(header);
}

bool CtamMiningBackend::SolveHeader(const util::SignalInterrupt& interrupt, CBlockHeader& header, uint64_t& max_tries, int expected_trunk_height, const uint256& expected_trunk_hash) const
{
    if (!Enabled()) {
        throw std::logic_error("CTAM mining backend is disabled");
    }
    if (max_tries == 0 || interrupt) {
        return false;
    }
    if (header.nNonce == std::numeric_limits<uint32_t>::max()) {
        return true;
    }

    CtamRestClient client{m_options.client_options};
    const auto status = client.Status();
    const CTAMDifficultyTriple difficulty = BuildCtamDifficulty(header, status.context_size);
    RequireCtamTrunkState(status, expected_trunk_height, expected_trunk_hash, difficulty.major_ctam_difficulty_context_size);
    const CtamHeaderMiningWork work = BuildHeaderWork(header);

    const uint32_t nonce_start = header.nNonce;
    const uint64_t nonce_budget_end = uint64_t{nonce_start} + max_tries - 1;
    const uint32_t nonce_end = static_cast<uint32_t>(std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), nonce_budget_end));

    CtamRestClient::MiningJob job = client.StartMiningJob(work.work_bytes, work.nonce_offset, nonce_start, nonce_end, difficulty, /*initial_state=*/{}, /*keep_branch=*/true);
    while (!job.IsTerminal()) {
        if (interrupt) {
            client.StopMiningJob(job.job_id);
            return false;
        }
        std::this_thread::sleep_for(CTAM_MINING_POLL_INTERVAL);
        job = client.GetMiningJob(job.job_id);
    }

    max_tries = job.attempts >= max_tries ? 0 : max_tries - job.attempts;

    if (job.status == "failed") {
        throw std::runtime_error(job.error.value_or("CTAM mining job failed"));
    }
    if (job.status == "stopped") {
        return false;
    }
    if (job.status != "completed") {
        throw std::runtime_error("CTAM mining job returned unexpected status: " + job.status);
    }
    if (!job.found) {
        return false;
    }
    if (!job.winning_nonce) {
        throw std::runtime_error("CTAM mining job completed without winning_nonce");
    }
    if (*job.winning_nonce < nonce_start || *job.winning_nonce > nonce_end) {
        throw std::runtime_error("CTAM mining job returned winning_nonce outside the requested range");
    }
    if (!job.digest) {
        throw std::runtime_error("CTAM mining job completed without digest metadata");
    }
    if (!job.branch_kept) {
        throw std::runtime_error("CTAM mining job found a nonce without retaining its winning branch");
    }
    if (job.branch_key == 0) {
        throw std::runtime_error("CTAM mining job returned an invalid winning branch key");
    }
    
    const auto final_status = client.Status();
    RequireCtamTrunkState(final_status, expected_trunk_height, expected_trunk_hash, difficulty.major_ctam_difficulty_context_size);

    CBlockHeader solved_header{header};
    solved_header.nNonce = *job.winning_nonce;
    const CtamHeaderMiningWork solved_work = BuildHeaderWork(solved_header);
    const auto replay = client.PostCtaSha256Message(solved_work.work_bytes, difficulty, /*keep_branch=*/false);
    if (replay.branch_kept) {
        throw std::runtime_error("CTAM solved-header replay unexpectedly retained a branch");
    }
    if (*job.digest != replay.digest) {
        throw std::runtime_error("CTAM mining job digest does not match solved-header replay");
    }
    if (!DigestMeetsTarget(replay.digest, difficulty.energy_difficulty)) {
        throw std::runtime_error("CTAM mining job digest does not satisfy the requested target");
    }

    const auto commit = client.CommitBranch(job.branch_key, static_cast<uint64_t>(expected_trunk_height + 1), solved_header.GetHash().ToString());
    if (commit.status != "committed") {
        throw std::runtime_error("CTAM winning branch commit failed with status: " + commit.status);
    }
    header.nNonce = solved_header.nNonce;
    return true;
}

bool CtamMiningBackend::SolveHeader(ChainstateManager& chainman, CBlockHeader& header, uint64_t& max_tries) const
{
    int expected_trunk_height{-1};
    uint256 expected_trunk_hash;
    {
        LOCK(::cs_main);
        const CBlockIndex* tip = chainman.ActiveChain().Tip();
        if (tip == nullptr) {
            throw std::runtime_error("Cannot mine with CTAM before BNG active chain has a tip");
        }
        expected_trunk_height = tip->nHeight;
        expected_trunk_hash = tip->GetBlockHash();
    }
    return SolveHeader(chainman.m_interrupt, header, max_tries, expected_trunk_height, expected_trunk_hash);
}

bool CtamMiningBackend::VerifyHeader(const CBlockHeader& header, int expected_trunk_height, const uint256& expected_trunk_hash) const
{
    if (!Enabled()) {
        return true;
    }

    try {
        CtamRestClient client{m_options.client_options};
        const auto status = client.Status();
        const CTAMDifficultyTriple difficulty = BuildCtamDifficulty(header, status.context_size);
        if (CtamTrunkMatchesBlock(status, expected_trunk_height + 1, header.GetHash(), difficulty.major_ctam_difficulty_context_size)) {
            return true;
        }
        RequireCtamTrunkState(status, expected_trunk_height, expected_trunk_hash, difficulty.major_ctam_difficulty_context_size);
        const CtamHeaderMiningWork work = BuildHeaderWork(header);
        const auto result = client.PostCtaSha256Message(work.work_bytes, difficulty, /*keep_branch=*/false);
        if (result.branch_kept) {
            throw std::runtime_error("CTAM verification replay unexpectedly retained a branch");
        }
        return DigestMeetsTarget(result.digest, difficulty.energy_difficulty);
    } catch (const std::exception& e) {
        LogWarning("CTAM proof-of-work validation failed: %s\n", e.what());
        return false;
    }
}

} // namespace node
