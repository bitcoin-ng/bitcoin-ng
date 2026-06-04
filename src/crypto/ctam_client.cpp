// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/ctam_client.h>

#include <support/events.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <event2/buffer.h>
#include <event2/http.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

struct HTTPReply {
    int status{0};
    int error{-1};
    std::string body;
};

std::string HTTPErrorString(int code)
{
    switch (code) {
    case EVREQ_HTTP_TIMEOUT:
        return "timeout reached";
    case EVREQ_HTTP_EOF:
        return "EOF reached";
    case EVREQ_HTTP_INVALID_HEADER:
        return "error while reading header, or invalid header";
    case EVREQ_HTTP_BUFFER_ERROR:
        return "error encountered while reading or writing";
    case EVREQ_HTTP_REQUEST_CANCEL:
        return "request was canceled";
    case EVREQ_HTTP_DATA_TOO_LONG:
        return "response body is larger than allowed";
    default:
        return "unknown";
    }
}

void HTTPRequestDone(struct evhttp_request* req, void* ctx)
{
    auto* reply = static_cast<HTTPReply*>(ctx);
    if (req == nullptr) {
        reply->status = 0;
        return;
    }

    reply->status = evhttp_request_get_response_code(req);
    if (auto* buf = evhttp_request_get_input_buffer(req)) {
        const std::size_t size = evbuffer_get_length(buf);
        if (const char* data = reinterpret_cast<const char*>(evbuffer_pullup(buf, size))) {
            reply->body = std::string(data, size);
        }
        evbuffer_drain(buf, size);
    }
}

void HTTPErrorCallback(enum evhttp_request_error err, void* ctx)
{
    static_cast<HTTPReply*>(ctx)->error = err;
}

struct ParsedURL {
    std::string host;
    uint16_t port{80};
    std::string target_prefix{"/"};
};

ParsedURL ParseHTTPURL(std::string_view base_url)
{
    constexpr std::string_view HTTP_PREFIX{"http://"};
    if (!base_url.starts_with(HTTP_PREFIX)) {
        throw std::runtime_error("CTAM REST URL must start with http://");
    }

    std::string_view remainder = base_url.substr(HTTP_PREFIX.size());
    const std::size_t slash = remainder.find('/');
    const std::string_view authority = remainder.substr(0, slash);
    std::string_view path = slash == std::string_view::npos ? std::string_view{} : remainder.substr(slash);

    ParsedURL parsed;
    if (!SplitHostPort(authority, parsed.port, parsed.host) || parsed.host.empty()) {
        throw std::runtime_error("Invalid CTAM REST URL host or port");
    }
    if (parsed.port == 0) {
        parsed.port = 80;
    }

    parsed.target_prefix = path.empty() ? "/" : std::string(path);
    if (!parsed.target_prefix.starts_with('/')) {
        parsed.target_prefix.insert(parsed.target_prefix.begin(), '/');
    }
    if (!parsed.target_prefix.ends_with('/')) {
        parsed.target_prefix.push_back('/');
    }

    return parsed;
}

std::string BuildTarget(std::string_view prefix, std::string_view path)
{
    if (!path.empty() && path.front() == '/') {
        path.remove_prefix(1);
    }
    std::string target(prefix);
    target.append(path);
    return target;
}

UniValue ParseJSONObject(std::string_view body)
{
    UniValue json;
    if (!json.read(std::string(body)) || !json.isObject()) {
        throw std::runtime_error("CTAM REST response is not a JSON object");
    }
    return json;
}

void ValidateDifficultyTriple(const CTAMDifficultyTriple& difficulty)
{
    if (difficulty.energy_difficulty_bits == 0) {
        throw std::runtime_error("CTAM energy difficulty bits must be non-zero");
    }
    if (difficulty.major_context_size < CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) {
        throw std::runtime_error("CTAM major difficulty must be at least context_size 27 (~16 MiB)");
    }
    if (difficulty.minor_rounds != CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS) {
        throw std::runtime_error("CTAM minor difficulty must match the consensus round count");
    }
    if (difficulty.minor_rounds > CTAMDifficultyTriple::MAX_MINOR_ROUNDS) {
        throw std::runtime_error("CTAM minor difficulty exceeds the SHA-256 round limit");
    }
}

} // namespace

CtamRestClient::CtamRestClient(const CTAMClientOptions& options)
    : m_timeout_ms{options.timeout_ms}
{
    if (m_timeout_ms <= 0) {
        throw std::runtime_error("CTAM timeout must be greater than zero milliseconds");
    }

    const ParsedURL parsed = ParseHTTPURL(options.base_url);
    m_host = parsed.host;
    m_port = parsed.port;
    m_target_prefix = parsed.target_prefix;
}

UniValue CtamRestClient::RequestJSON(enum evhttp_cmd_type method, std::string_view path, std::string_view body) const
{
    raii_event_base base = obtain_event_base();
    raii_evhttp_connection connection = obtain_evhttp_connection_base(base.get(), m_host, m_port);
    evhttp_connection_set_timeout(connection.get(), std::max(1, static_cast<int>(std::ceil(m_timeout_ms / 1000.0))));

    HTTPReply response;
    raii_evhttp_request request = obtain_evhttp_request(HTTPRequestDone, &response);
    if (request == nullptr) {
        throw std::runtime_error("Failed to create CTAM REST request");
    }

    evhttp_request_set_error_cb(request.get(), HTTPErrorCallback);

    if (auto* headers = evhttp_request_get_output_headers(request.get())) {
        evhttp_add_header(headers, "Host", m_host.c_str());
        evhttp_add_header(headers, "Connection", "close");
        if (method == EVHTTP_REQ_POST) {
            evhttp_add_header(headers, "Content-Type", "application/json");
        }
    }

    if (!body.empty()) {
        if (auto* output = evhttp_request_get_output_buffer(request.get())) {
            evbuffer_add(output, body.data(), body.size());
        }
    }

    if (evhttp_make_request(connection.get(), request.get(), method, BuildTarget(m_target_prefix, path).c_str()) != 0) {
        throw std::runtime_error("Failed to dispatch CTAM REST request");
    }
    request.release();
    event_base_dispatch(base.get());

    if (response.status == 0) {
        throw std::runtime_error("Could not connect to CTAM REST service at " + m_host + ":" + std::to_string(m_port) +
                                 " (" + HTTPErrorString(response.error) + ")");
    }
    if (response.status != HTTP_OK) {
        throw std::runtime_error("CTAM REST endpoint returned HTTP " + std::to_string(response.status));
    }

    return ParseJSONObject(response.body);
}

UniValue CtamRestClient::Health() const
{
    return RequestJSON(EVHTTP_REQ_GET, "health");
}

std::array<unsigned char, CtamRestClient::OUTPUT_SIZE> CtamRestClient::CTA_SHA256(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch) const
{
    ValidateDifficultyTriple(difficulty);

    UniValue payload(UniValue::VOBJ);
    payload.pushKV("message_hex", HexStr(message));
    payload.pushKV("energy_difficulty_bits", difficulty.energy_difficulty_bits);
    payload.pushKV("major_ctam_difficulty_context_size", difficulty.major_context_size);
    payload.pushKV("minor_ctam_difficulty_rounds", difficulty.minor_rounds);
    payload.pushKV("injections", difficulty.minor_rounds);
    payload.pushKV("keep_branch", keep_branch);

    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "crypto/cta_sha256", payload.write());
    if (!response.exists("digest_hex") || !response["digest_hex"].isStr()) {
        throw std::runtime_error("CTAM /crypto/cta_sha256 did not return digest_hex");
    }

    const auto digest = TryParseHex<unsigned char>(response["digest_hex"].get_str());
    if (!digest || digest->size() != OUTPUT_SIZE) {
        throw std::runtime_error("CTAM /crypto/cta_sha256 returned an invalid digest");
    }

    std::array<unsigned char, OUTPUT_SIZE> result;
    std::copy(digest->begin(), digest->end(), result.begin());
    return result;
}

CtamRestClient::MineResult CtamRestClient::MineCSHA256(std::span<const unsigned char> work_bytes,
                                                       uint32_t nonce_offset,
                                                       uint32_t nonce_start,
                                                       uint32_t nonce_end,
                                                       const CTAMDifficultyTriple& difficulty,
                                                       std::span<const unsigned char> initial_state,
                                                       bool keep_branch) const
{
    ValidateDifficultyTriple(difficulty);

    if (work_bytes.empty()) {
        throw std::runtime_error("CTAM mining requires non-empty work bytes");
    }
    if (initial_state.size() != 0 && initial_state.size() != OUTPUT_SIZE) {
        throw std::runtime_error("CTAM mining initial state must be empty or exactly 32 bytes");
    }
    if (nonce_offset > work_bytes.size() - sizeof(uint32_t)) {
        throw std::runtime_error("CTAM mining nonce offset must leave room for 4 nonce bytes inside the supplied work bytes");
    }
    if (nonce_end < nonce_start) {
        throw std::runtime_error("CTAM mining nonce_end must be greater than or equal to nonce_start");
    }

    UniValue payload(UniValue::VOBJ);
    payload.pushKV("work_bytes_hex", HexStr(work_bytes));
    payload.pushKV("nonce_offset", nonce_offset);
    payload.pushKV("nonce_start", nonce_start);
    payload.pushKV("nonce_end", nonce_end);
    payload.pushKV("energy_difficulty_bits", difficulty.energy_difficulty_bits);
    payload.pushKV("major_ctam_difficulty_context_size", difficulty.major_context_size);
    payload.pushKV("minor_ctam_difficulty_rounds", difficulty.minor_rounds);
    payload.pushKV("injections", difficulty.minor_rounds);
    payload.pushKV("keep_branch", keep_branch);
    if (!initial_state.empty()) {
        payload.pushKV("initial_state_hex", HexStr(initial_state));
    }

    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "crypto/mine_csha256", payload.write());
    if (!response.exists("found") || !response["found"].isBool()) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 did not return found");
    }
    if (!response.exists("branch_key") || !response["branch_key"].isNum()) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 did not return branch_key");
    }
    if (!response.exists("branch_kept") || !response["branch_kept"].isBool()) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 did not return branch_kept");
    }
    if (!response.exists("attempts") || !response["attempts"].isNum()) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 did not return attempts");
    }

    MineResult result{
        .found = response["found"].get_bool(),
        .digest = std::nullopt,
        .branch_key = static_cast<uint32_t>(response["branch_key"].getInt<int64_t>()),
        .branch_kept = response["branch_kept"].get_bool(),
        .winning_nonce = std::nullopt,
        .nonce_start = nonce_start,
        .nonce_end = nonce_end,
        .nonce_offset = nonce_offset,
        .attempts = static_cast<uint64_t>(response["attempts"].getInt<int64_t>()),
        .energy_difficulty_bits = difficulty.energy_difficulty_bits,
        .major_context_size = difficulty.major_context_size,
        .minor_rounds = difficulty.minor_rounds,
    };

    if (response.exists("digest_hex") && !response["digest_hex"].isNull()) {
        if (!response["digest_hex"].isStr()) {
            throw std::runtime_error("CTAM /crypto/mine_csha256 returned a non-string digest_hex");
        }
        const auto digest = TryParseHex<unsigned char>(response["digest_hex"].get_str());
        if (!digest || digest->size() != OUTPUT_SIZE) {
            throw std::runtime_error("CTAM /crypto/mine_csha256 returned an invalid digest");
        }
        std::array<unsigned char, OUTPUT_SIZE> digest_array;
        std::copy(digest->begin(), digest->end(), digest_array.begin());
        result.digest = digest_array;
    }

    if (response.exists("winning_nonce") && !response["winning_nonce"].isNull()) {
        if (!response["winning_nonce"].isNum()) {
            throw std::runtime_error("CTAM /crypto/mine_csha256 returned a non-numeric winning_nonce");
        }
        const int64_t winning_nonce = response["winning_nonce"].getInt<int64_t>();
        if (winning_nonce < 0 || winning_nonce > std::numeric_limits<uint32_t>::max()) {
            throw std::runtime_error("CTAM /crypto/mine_csha256 returned an out-of-range winning_nonce");
        }
        result.winning_nonce = static_cast<uint32_t>(winning_nonce);
    }

    if (result.found && (!result.digest.has_value() || !result.winning_nonce.has_value())) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 reported a solution without digest or winning_nonce");
    }
    if (!result.found && (result.digest.has_value() || result.winning_nonce.has_value())) {
        throw std::runtime_error("CTAM /crypto/mine_csha256 reported no solution but returned solution fields");
    }

    return result;
}