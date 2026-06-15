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

void ValidateMiningInputs(std::span<const unsigned char> work_bytes,
                          uint32_t nonce_offset,
                          uint32_t nonce_start,
                          uint32_t nonce_end,
                          std::span<const unsigned char> initial_state)
{
    if (work_bytes.empty()) {
        throw std::runtime_error("CTAM mining requires non-empty work bytes");
    }
    if (initial_state.size() != 0 && initial_state.size() != CtamRestClient::OUTPUT_SIZE) {
        throw std::runtime_error("CTAM mining initial state must be empty or exactly 32 bytes");
    }
    if (nonce_offset > work_bytes.size() - sizeof(uint32_t)) {
        throw std::runtime_error("CTAM mining nonce offset must leave room for 4 nonce bytes inside the supplied work bytes");
    }
    if (nonce_end < nonce_start) {
        throw std::runtime_error("CTAM mining nonce_end must be greater than or equal to nonce_start");
    }
}

UniValue BuildMiningPayload(std::span<const unsigned char> work_bytes,
                            uint32_t nonce_offset,
                            uint32_t nonce_start,
                            uint32_t nonce_end,
                            const CTAMDifficultyTriple& difficulty,
                            std::span<const unsigned char> initial_state,
                            bool keep_branch)
{
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
    return payload;
}

int64_t RequireJSONInt(const UniValue& response, std::string_view field, std::string_view endpoint)
{
    const std::string field_name{field};
    if (!response.exists(field_name) || !response[field_name].isNum()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " did not return numeric " + field_name);
    }
    return response[field_name].getInt<int64_t>();
}

uint32_t RequireJSONUInt32(const UniValue& response, std::string_view field, std::string_view endpoint)
{
    const int64_t value = RequireJSONInt(response, field, endpoint);
    if (value < 0 || value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned out-of-range " + std::string(field));
    }
    return static_cast<uint32_t>(value);
}

uint64_t RequireJSONUInt64(const UniValue& response, std::string_view field, std::string_view endpoint)
{
    const int64_t value = RequireJSONInt(response, field, endpoint);
    if (value < 0) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned negative " + std::string(field));
    }
    return static_cast<uint64_t>(value);
}

bool RequireJSONBool(const UniValue& response, std::string_view field, std::string_view endpoint)
{
    const std::string field_name{field};
    if (!response.exists(field_name) || !response[field_name].isBool()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " did not return boolean " + field_name);
    }
    return response[field_name].get_bool();
}

std::string RequireJSONString(const UniValue& response, std::string_view field, std::string_view endpoint)
{
    const std::string field_name{field};
    if (!response.exists(field_name) || !response[field_name].isStr()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " did not return string " + field_name);
    }
    return response[field_name].get_str();
}

std::optional<std::array<unsigned char, CtamRestClient::OUTPUT_SIZE>> ParseOptionalDigest(const UniValue& response, std::string_view endpoint)
{
    if (!response.exists("digest_hex") || response["digest_hex"].isNull()) {
        return std::nullopt;
    }
    if (!response["digest_hex"].isStr()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned a non-string digest_hex");
    }
    const auto digest = TryParseHex<unsigned char>(response["digest_hex"].get_str());
    if (!digest || digest->size() != CtamRestClient::OUTPUT_SIZE) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned an invalid digest");
    }
    std::array<unsigned char, CtamRestClient::OUTPUT_SIZE> digest_array;
    std::copy(digest->begin(), digest->end(), digest_array.begin());
    return digest_array;
}

std::optional<uint32_t> ParseOptionalNonce(const UniValue& response, std::string_view endpoint)
{
    if (!response.exists("winning_nonce") || response["winning_nonce"].isNull()) {
        return std::nullopt;
    }
    if (!response["winning_nonce"].isNum()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned a non-numeric winning_nonce");
    }
    const int64_t winning_nonce = response["winning_nonce"].getInt<int64_t>();
    if (winning_nonce < 0 || winning_nonce > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned an out-of-range winning_nonce");
    }
    return static_cast<uint32_t>(winning_nonce);
}

CtamRestClient::MiningJob ParseMiningJob(const UniValue& response, std::string_view endpoint)
{
    const uint32_t minor_rounds = RequireJSONUInt32(response, "minor_ctam_difficulty_rounds", endpoint);
    if (minor_rounds > CTAMDifficultyTriple::MAX_MINOR_ROUNDS) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned out-of-range minor_ctam_difficulty_rounds");
    }

    CtamRestClient::MiningJob job{
        .job_id = RequireJSONString(response, "job_id", endpoint),
        .status = RequireJSONString(response, "status", endpoint),
        .found = RequireJSONBool(response, "found", endpoint),
        .digest = ParseOptionalDigest(response, endpoint),
        .winning_nonce = ParseOptionalNonce(response, endpoint),
        .attempts = RequireJSONUInt64(response, "attempts", endpoint),
        .nonce_start = RequireJSONUInt32(response, "nonce_start", endpoint),
        .nonce_end = RequireJSONUInt32(response, "nonce_end", endpoint),
        .nonce_offset = RequireJSONUInt32(response, "nonce_offset", endpoint),
        .branch_key = RequireJSONUInt64(response, "branch_key", endpoint),
        .branch_kept = RequireJSONBool(response, "branch_kept", endpoint),
        .energy_difficulty_bits = RequireJSONUInt32(response, "energy_difficulty_bits", endpoint),
        .major_context_size = RequireJSONUInt32(response, "major_ctam_difficulty_context_size", endpoint),
        .minor_rounds = static_cast<uint8_t>(minor_rounds),
        .injections = RequireJSONUInt32(response, "injections", endpoint),
        .rounds = RequireJSONUInt32(response, "rounds", endpoint),
        .error = std::nullopt,
    };

    if (response.exists("error") && !response["error"].isNull()) {
        if (!response["error"].isStr()) {
            throw std::runtime_error("CTAM " + std::string(endpoint) + " returned a non-string error");
        }
        job.error = response["error"].get_str();
    }
    if (job.minor_rounds != job.injections) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " returned mismatched minor rounds and injections");
    }
    if (job.found && (!job.digest.has_value() || !job.winning_nonce.has_value())) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " reported a solution without digest or winning_nonce");
    }
    if (!job.found && (job.digest.has_value() || job.winning_nonce.has_value())) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " reported no solution but returned solution fields");
    }

    return job;
}

CtamRestClient::StatusResult ParseStatus(const UniValue& response, std::string_view endpoint)
{
    return {
        .ctam_version = RequireJSONString(response, "ctam_version", endpoint),
        .status = RequireJSONString(response, "status", endpoint),
        .context_size = RequireJSONUInt32(response, "context_size", endpoint),
        .trunk_key = RequireJSONUInt64(response, "trunk_key", endpoint),
        .branch_count = RequireJSONUInt64(response, "branch_count", endpoint),
    };
}

CtamRestClient::CtaSha256Result ParseCtaSha256Result(const UniValue& response, std::string_view endpoint)
{
    const auto digest = ParseOptionalDigest(response, endpoint);
    if (!digest) {
        throw std::runtime_error("CTAM " + std::string(endpoint) + " did not return digest_hex");
    }

    return {
        .digest = *digest,
        .branch_key = RequireJSONUInt64(response, "branch_key", endpoint),
        .branch_kept = RequireJSONBool(response, "branch_kept", endpoint),
        .injections = RequireJSONUInt32(response, "injections", endpoint),
        .rounds = RequireJSONUInt32(response, "rounds", endpoint),
    };
}

} // namespace

bool CtamRestClient::MiningJob::IsTerminal() const
{
    return status == "completed" || status == "stopped" || status == "failed";
}

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

UniValue CtamRestClient::RequestJSON(int method, std::string_view path, std::string_view body) const
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

    if (evhttp_make_request(connection.get(), request.get(), static_cast<evhttp_cmd_type>(method), BuildTarget(m_target_prefix, path).c_str()) != 0) {
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

CtamRestClient::StatusResult CtamRestClient::Status() const
{
    return ParseStatus(RequestJSON(EVHTTP_REQ_GET, "status"), "/status");
}

CtamRestClient::CtaSha256Result CtamRestClient::CtaSha256Replay(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch) const
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
    CtaSha256Result result = ParseCtaSha256Result(response, "/crypto/cta_sha256");
    if (result.injections != difficulty.minor_rounds) {
        throw std::runtime_error("CTAM /crypto/cta_sha256 returned injections metadata that does not match the request");
    }

    return result;
}

CtamRestClient::CtaSha256Result CtamRestClient::CtaSha256WorkBytesReplay(std::span<const unsigned char> work_bytes, const CTAMDifficultyTriple& difficulty, bool keep_branch) const
{
    ValidateDifficultyTriple(difficulty);

    UniValue payload(UniValue::VOBJ);
    payload.pushKV("work_bytes_hex", HexStr(work_bytes));
    payload.pushKV("energy_difficulty_bits", difficulty.energy_difficulty_bits);
    payload.pushKV("major_ctam_difficulty_context_size", difficulty.major_context_size);
    payload.pushKV("minor_ctam_difficulty_rounds", difficulty.minor_rounds);
    payload.pushKV("injections", difficulty.minor_rounds);
    payload.pushKV("keep_branch", keep_branch);

    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "crypto/cta_sha256", payload.write());
    CtaSha256Result result = ParseCtaSha256Result(response, "/crypto/cta_sha256");
    if (result.injections != difficulty.minor_rounds) {
        throw std::runtime_error("CTAM /crypto/cta_sha256 returned injections metadata that does not match the request");
    }

    return result;
}

std::array<unsigned char, CtamRestClient::OUTPUT_SIZE> CtamRestClient::CTA_SHA256(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty, bool keep_branch) const
{
    return CtaSha256Replay(message, difficulty, keep_branch).digest;
}

CtamRestClient::CtaSha256Result CtamRestClient::CtaSha256KeepBranch(std::span<const unsigned char> message, const CTAMDifficultyTriple& difficulty) const
{
    CtaSha256Result result = CtaSha256Replay(message, difficulty, /*keep_branch=*/true);
    if (!result.branch_kept) {
        throw std::runtime_error("CTAM /crypto/cta_sha256 did not keep the replay branch");
    }
    return result;
}

CtamRestClient::CommitBranchResult CtamRestClient::CommitBranch(uint64_t branch_key) const
{
    if (branch_key == 0) {
        throw std::runtime_error("CTAM branch commit requires a non-zero branch key");
    }

    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "branches/" + std::to_string(branch_key) + "/commit", "{}");
    return {
        .status = RequireJSONString(response, "status", "/branches/{branch_key}/commit"),
    };
}

CtamRestClient::StatusResult CtamRestClient::EnsureReadyForMining(const CTAMDifficultyTriple& difficulty) const
{
    ValidateDifficultyTriple(difficulty);

    const StatusResult status = Status();
    if (status.status != "running") {
        throw std::runtime_error("CTAM status is not running");
    }
    if (status.context_size < difficulty.major_context_size) {
        throw std::runtime_error("CTAM context_size is below the requested major difficulty");
    }
    if (difficulty.major_context_size < CTAMDifficultyTriple::MIN_MAJOR_CONTEXT_SIZE) {
        throw std::runtime_error("CTAM major difficulty is below the BNG consensus minimum");
    }
    return status;
}

CtamRestClient::MiningJob CtamRestClient::StartMiningJob(std::span<const unsigned char> work_bytes,
                                                         uint32_t nonce_offset,
                                                         uint32_t nonce_start,
                                                         uint32_t nonce_end,
                                                         const CTAMDifficultyTriple& difficulty,
                                                         std::span<const unsigned char> initial_state,
                                                         bool keep_branch) const
{
    ValidateDifficultyTriple(difficulty);
    ValidateMiningInputs(work_bytes, nonce_offset, nonce_start, nonce_end, initial_state);

    const UniValue payload = BuildMiningPayload(work_bytes, nonce_offset, nonce_start, nonce_end, difficulty, initial_state, keep_branch);
    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "mining", payload.write());
    MiningJob job = ParseMiningJob(response, "/mining");

    if (job.nonce_start != nonce_start || job.nonce_end != nonce_end || job.nonce_offset != nonce_offset) {
        throw std::runtime_error("CTAM /mining returned nonce metadata that does not match the request");
    }
    if (job.energy_difficulty_bits != difficulty.energy_difficulty_bits ||
        job.major_context_size != difficulty.major_context_size ||
        job.minor_rounds != difficulty.minor_rounds) {
        throw std::runtime_error("CTAM /mining returned a difficulty tuple that does not match the request");
    }

    return job;
}

CtamRestClient::MiningJob CtamRestClient::GetMiningJob(std::string_view job_id) const
{
    if (job_id.empty()) {
        throw std::runtime_error("CTAM mining job id must not be empty");
    }

    const UniValue response = RequestJSON(EVHTTP_REQ_GET, "mining/" + std::string(job_id));
    MiningJob job = ParseMiningJob(response, "/mining/{job_id}");
    if (job.job_id != job_id) {
        throw std::runtime_error("CTAM /mining/{job_id} returned a different job_id");
    }
    return job;
}

CtamRestClient::MiningJob CtamRestClient::StopMiningJob(std::string_view job_id) const
{
    if (job_id.empty()) {
        throw std::runtime_error("CTAM mining job id must not be empty");
    }

    const UniValue response = RequestJSON(EVHTTP_REQ_POST, "mining/" + std::string(job_id) + "/stop", "{}");
    MiningJob job = ParseMiningJob(response, "/mining/{job_id}/stop");
    if (job.job_id != job_id) {
        throw std::runtime_error("CTAM /mining/{job_id}/stop returned a different job_id");
    }
    return job;
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
    ValidateMiningInputs(work_bytes, nonce_offset, nonce_start, nonce_end, initial_state);

    const UniValue payload = BuildMiningPayload(work_bytes, nonce_offset, nonce_start, nonce_end, difficulty, initial_state, keep_branch);
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
        .branch_key = RequireJSONUInt64(response, "branch_key", "/crypto/mine_csha256"),
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
