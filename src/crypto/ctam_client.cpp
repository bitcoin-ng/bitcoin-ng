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