// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CTAM_CLIENT_H
#define BITCOIN_CRYPTO_CTAM_CLIENT_H

#include <span.h>

#include <event2/http.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

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

    explicit CtamRestClient(const CTAMClientOptions& options);

    UniValue Health() const;
    std::array<unsigned char, OUTPUT_SIZE> CTA_SHA256(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch = false) const;

private:
    UniValue RequestJSON(enum evhttp_cmd_type method, std::string_view path, std::string_view body = {}) const;

    std::string m_host;
    uint16_t m_port;
    std::string m_target_prefix;
    int m_timeout_ms;
};

#endif // BITCOIN_CRYPTO_CTAM_CLIENT_H