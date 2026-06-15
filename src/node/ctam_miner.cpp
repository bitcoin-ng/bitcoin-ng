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
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <thread>

namespace node {
namespace {

constexpr int DEFAULT_CTAM_TIMEOUT_MS{5000};
constexpr auto CTAM_MINING_POLL_INTERVAL{std::chrono::milliseconds{50}};

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
        .energy_difficulty_bits = header.nBits,
        .major_context_size = context_size,
        .minor_rounds = CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS,
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

bool CtamMiningBackend::SolveHeader(ChainstateManager& chainman, CBlockHeader& header, uint64_t& max_tries) const
{
    if (!Enabled()) {
        throw std::logic_error("CTAM mining backend is disabled");
    }
    if (max_tries == 0 || chainman.m_interrupt) {
        return false;
    }
    if (header.nNonce == std::numeric_limits<uint32_t>::max()) {
        return true;
    }

    CtamRestClient client{m_options.client_options};
    const CTAMDifficultyTriple difficulty = BuildCtamDifficulty(header, client.Status().context_size);
    client.EnsureReadyForMining(difficulty);
    const CtamHeaderMiningWork work = BuildHeaderWork(header);

    const uint32_t nonce_start = header.nNonce;
    const uint64_t max_nonce_end = std::min<uint64_t>(std::numeric_limits<uint32_t>::max(), uint64_t{nonce_start} + max_tries);
    const uint32_t nonce_end = static_cast<uint32_t>(max_nonce_end);

    CtamRestClient::MiningJob job = client.StartMiningJob(work.work_bytes, work.nonce_offset, nonce_start, nonce_end, difficulty);
    while (!job.IsTerminal()) {
        if (chainman.m_interrupt) {
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

    header.nNonce = *job.winning_nonce;
    return true;
}

bool CtamMiningBackend::VerifyHeader(const CBlockHeader& header) const
{
    if (!Enabled()) {
        return true;
    }

    try {
        CtamRestClient client{m_options.client_options};
        const CTAMDifficultyTriple difficulty = BuildCtamDifficulty(header, client.Status().context_size);
        client.EnsureReadyForMining(difficulty);
        const CtamHeaderMiningWork work = BuildHeaderWork(header);
        const auto result = client.MineCSHA256(work.work_bytes, work.nonce_offset, header.nNonce, header.nNonce, difficulty);
        return result.found && result.winning_nonce && *result.winning_nonce == header.nNonce;
    } catch (const std::exception& e) {
        LogWarning("CTAM proof-of-work validation failed: %s\n", e.what());
        return false;
    }
}

} // namespace node
