// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chain.h>
#include <common/args.h>
#include <compat/compat.h>
#include <crypto/ctam_client.h>
#include <node/context.h>
#include <node/ctam_miner.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/string.h>

#include <boost/test/unit_test.hpp>

#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t EASY_ENERGY_DIFFICULTY{0x207fffff};
constexpr uint32_t CTAM_CONTEXT_SIZE{27};
constexpr uint8_t CTAM_MINOR_ROUNDS{CTAMDifficultyTriple::CONSENSUS_MINOR_ROUNDS};

struct MockReply {
    evhttp_cmd_type method;
    std::string path;
    std::string body;
    int status{HTTP_OK};
};

struct MockRequest {
    evhttp_cmd_type method;
    std::string path;
    std::string body;
};

class MockCtamServer
{
public:
    explicit MockCtamServer(std::deque<MockReply> replies)
        : m_replies{std::move(replies)}
    {
        m_base = event_base_new();
        if (m_base == nullptr) {
            throw std::runtime_error("failed to create mock CTAM event base");
        }
        m_http = evhttp_new(m_base);
        if (m_http == nullptr) {
            event_base_free(m_base);
            throw std::runtime_error("failed to create mock CTAM HTTP server");
        }
        evhttp_set_gencb(m_http, &MockCtamServer::HandleRequest, this);
        evhttp_bound_socket* bound = evhttp_bind_socket_with_handle(m_http, "127.0.0.1", 0);
        if (bound == nullptr) {
            evhttp_free(m_http);
            event_base_free(m_base);
            throw std::runtime_error("failed to bind mock CTAM HTTP server");
        }

        sockaddr_in addr{};
        socklen_t addr_len{sizeof(addr)};
        if (getsockname(evhttp_bound_socket_get_fd(bound), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
            evhttp_free(m_http);
            event_base_free(m_base);
            throw std::runtime_error("failed to inspect mock CTAM HTTP port");
        }
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this] {
            while (!m_stop.load()) {
                event_base_loop(m_base, EVLOOP_NONBLOCK);
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
        });
    }

    ~MockCtamServer()
    {
        m_stop = true;
        if (m_thread.joinable()) {
            m_thread.join();
        }
        evhttp_free(m_http);
        event_base_free(m_base);
    }

    std::string Url() const { return strprintf("http://127.0.0.1:%u/", m_port); }

    std::vector<MockRequest> Requests() const
    {
        std::lock_guard lock{m_mutex};
        return m_requests;
    }

    bool SawUnexpectedRequest() const
    {
        std::lock_guard lock{m_mutex};
        return m_unexpected;
    }

private:
    static std::string ReadBody(evhttp_request* req)
    {
        evbuffer* input = evhttp_request_get_input_buffer(req);
        const size_t size = input == nullptr ? 0 : evbuffer_get_length(input);
        if (size == 0) {
            return {};
        }
        const char* data = reinterpret_cast<const char*>(evbuffer_pullup(input, size));
        std::string body{data, size};
        evbuffer_drain(input, size);
        return body;
    }

    static void Send(evhttp_request* req, int status, const std::string& body)
    {
        evbuffer* output = evbuffer_new();
        evbuffer_add(output, body.data(), body.size());
        evhttp_add_header(evhttp_request_get_output_headers(req), "Content-Type", "application/json");
        evhttp_send_reply(req, status, "OK", output);
        evbuffer_free(output);
    }

    static void HandleRequest(evhttp_request* req, void* ctx)
    {
        static_cast<MockCtamServer*>(ctx)->HandleRequest(req);
    }

    void HandleRequest(evhttp_request* req)
    {
        const evhttp_cmd_type method = evhttp_request_get_command(req);
        const std::string path{evhttp_request_get_uri(req)};
        const std::string body = ReadBody(req);

        MockReply reply;
        {
            std::lock_guard lock{m_mutex};
            m_requests.push_back({method, path, body});
            if (m_replies.empty()) {
                m_unexpected = true;
                reply = {method, path, R"({"error":"unexpected request"})", HTTP_INTERNAL};
            } else {
                reply = std::move(m_replies.front());
                m_replies.pop_front();
                if (reply.method != method || reply.path != path) {
                    m_unexpected = true;
                }
            }
        }
        Send(req, reply.status, reply.body);
    }

    event_base* m_base{nullptr};
    evhttp* m_http{nullptr};
    uint16_t m_port{0};
    std::thread m_thread;
    std::atomic<bool> m_stop{false};

    mutable std::mutex m_mutex;
    std::deque<MockReply> m_replies;
    std::vector<MockRequest> m_requests;
    bool m_unexpected{false};
};

std::string StatusJson(uint64_t bng_height = 0, std::string bng_block_hash = uint256::ONE.ToString(), uint32_t context_size = CTAM_CONTEXT_SIZE)
{
    return strprintf(R"({"ctam_version":"0.1.0","status":"running","context_size":%u,"trunk_key":0,"bng_height":%u,"bng_block_hash":"%s","branch_count":0})", context_size, bng_height, bng_block_hash);
}

std::string StatusJsonWithoutBngMetadata()
{
    return strprintf(R"({"ctam_version":"0.1.0","status":"running","context_size":%u,"trunk_key":0,"bng_height":null,"bng_block_hash":null,"branch_count":0})", CTAM_CONTEXT_SIZE);
}

std::string StatusJsonWithoutHeight()
{
    return strprintf(R"({"ctam_version":"0.1.0","status":"running","context_size":%u,"trunk_key":0,"bng_block_hash":"%s","branch_count":0})", CTAM_CONTEXT_SIZE, uint256::ONE.ToString());
}

std::string StatusJsonWithoutBlockHash()
{
    return strprintf(R"({"ctam_version":"0.1.0","status":"running","context_size":%u,"trunk_key":0,"bng_height":0,"branch_count":0})", CTAM_CONTEXT_SIZE);
}

std::string JobJson(std::string status, bool found, uint64_t branch_key, bool branch_kept, std::string winning_nonce, uint64_t attempts)
{
    const std::string digest = found ? R"("0102030405060708090001020304050607080900010203040506070809000102")" : "null";
    return strprintf(
        R"({"job_id":"1","status":"%s","found":%s,"digest_hex":%s,"winning_nonce":%s,"attempts":%u,"nonce_start":0,"nonce_end":99,"nonce_offset":76,"branch_key":%u,"branch_kept":%s,"energy_difficulty":%u,"context_size_difficulty":%u,"injection_rounds_difficulty":%u,"rounds":64,"error":null})",
        status,
        found ? "true" : "false",
        digest,
        winning_nonce,
        attempts,
        branch_key,
        branch_kept ? "true" : "false",
        EASY_ENERGY_DIFFICULTY,
        CTAM_CONTEXT_SIZE,
        CTAM_MINOR_ROUNDS);
}

CBlockHeader Header()
{
    CBlockHeader header;
    header.nVersion = 4;
    header.hashPrevBlock = uint256::ONE;
    header.hashMerkleRoot = uint256::ONE;
    header.nTime = 1'700'000'000;
    header.nBits = EASY_ENERGY_DIFFICULTY;
    header.nNonce = 0;
    return header;
}

void ConfigureArgs(ArgsManager& args, const MockCtamServer& server)
{
    args.ForceSetArg("-ctammining", "1");
    args.ForceSetArg("-ctamresturl", server.Url());
    args.ForceSetArg("-ctamtimeoutms", "5000");
}

UniValue ParseBody(const std::string& body)
{
    UniValue value;
    BOOST_REQUIRE(value.read(body));
    BOOST_REQUIRE(value.isObject());
    return value;
}

bool HasBranchCommitRequest(const std::vector<MockRequest>& requests)
{
    for (const auto& request : requests) {
        if (request.method == EVHTTP_REQ_POST && request.path.find("/branches/") == 0) {
            return true;
        }
    }
    return false;
}

bool HasRequestPath(const std::vector<MockRequest>& requests, const std::string& path)
{
    for (const auto& request : requests) {
        if (request.path == path) {
            return true;
        }
    }
    return false;
}

size_t CountRequestPath(const std::vector<MockRequest>& requests, const std::string& path)
{
    return std::count_if(requests.begin(), requests.end(), [&path](const MockRequest& request) {
        return request.path == path;
    });
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(ctam_miner_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(options_difficulty_and_header_work)
{
    ArgsManager args;
    args.ForceSetArg("-ctammining", "0");
    args.ForceSetArg("-ctamresturl", "http://127.0.0.1:18080/base/");
    args.ForceSetArg("-ctamtimeoutms", "1234");

    const auto options = node::GetCtamMiningOptions(args);
    BOOST_CHECK(!options.enabled);
    BOOST_CHECK_EQUAL(options.client_options.base_url, "http://127.0.0.1:18080/base/");
    BOOST_CHECK_EQUAL(options.client_options.timeout_ms, 1234);

    const CBlockHeader header = Header();
    const auto difficulty = node::BuildCtamDifficulty(header, CTAM_CONTEXT_SIZE);
    BOOST_CHECK_EQUAL(difficulty.energy_difficulty, header.nBits);
    BOOST_CHECK_EQUAL(difficulty.major_ctam_difficulty_context_size, CTAM_CONTEXT_SIZE);
    BOOST_CHECK_EQUAL(difficulty.minor_ctam_difficulty_rounds, CTAM_MINOR_ROUNDS);

    const auto work = node::CtamMiningBackend{args}.BuildHeaderWork(header);
    BOOST_CHECK_EQUAL(work.work_bytes.size(), 80U);
    BOOST_CHECK_EQUAL(work.nonce_offset, 76U);
}

BOOST_AUTO_TEST_CASE(solve_header_commits_winning_branch)
{
    MockCtamServer server{{
        {EVHTTP_REQ_GET, "/status", StatusJsonWithoutBngMetadata()},
        {EVHTTP_REQ_POST, "/mining", JobJson("running", false, 42, false, "null", 0)},
        {EVHTTP_REQ_GET, "/mining/1", JobJson("completed", true, 42, true, "7", 8)},
        {EVHTTP_REQ_GET, "/status", StatusJsonWithoutBngMetadata()},
        {EVHTTP_REQ_POST, "/crypto/cta_sha256", R"({"digest_hex":"0102030405060708090001020304050607080900010203040506070809000102","branch_key":43,"branch_kept":false,"injection_rounds_difficulty":16,"rounds":64})"},
        {EVHTTP_REQ_POST, "/branches/42/commit", R"({"status":"committed"})"},
    }};

    CBlockHeader header = Header();
    uint64_t max_tries{100};
    ArgsManager args;
    ConfigureArgs(args, server);
    BOOST_CHECK(node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE));
    BOOST_CHECK_EQUAL(header.nNonce, 7U);
    BOOST_CHECK_EQUAL(max_tries, 92U);

    const auto requests = server.Requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 6U);
    BOOST_CHECK_EQUAL(CountRequestPath(requests, "/status"), 2U);
    BOOST_CHECK_EQUAL(CountRequestPath(requests, "/mining"), 1U);
    BOOST_CHECK_EQUAL(CountRequestPath(requests, "/mining/1"), 1U);
    BOOST_CHECK_EQUAL(CountRequestPath(requests, "/crypto/cta_sha256"), 1U);
    BOOST_CHECK_EQUAL(CountRequestPath(requests, "/branches/42/commit"), 1U);
    BOOST_CHECK_EQUAL(requests[0].path, "/status");
    BOOST_CHECK_EQUAL(requests[1].path, "/mining");
    const UniValue mining_request = ParseBody(requests[1].body);
    BOOST_CHECK_EQUAL(mining_request["nonce_offset"].getInt<int>(), 76);
    BOOST_CHECK_EQUAL(mining_request["energy_difficulty"].getInt<int>(), static_cast<int>(EASY_ENERGY_DIFFICULTY));
    BOOST_CHECK(!mining_request.exists("energy_difficulty_bits"));
    BOOST_CHECK_EQUAL(mining_request["context_size_difficulty"].getInt<int>(), static_cast<int>(CTAM_CONTEXT_SIZE));
    BOOST_CHECK_EQUAL(mining_request["injection_rounds_difficulty"].getInt<int>(), static_cast<int>(CTAM_MINOR_ROUNDS));
    BOOST_REQUIRE(mining_request["keep_branch"].isBool());
    BOOST_CHECK(mining_request["keep_branch"].get_bool());
    BOOST_CHECK_EQUAL(requests[3].path, "/status");
    BOOST_CHECK_EQUAL(requests[4].path, "/crypto/cta_sha256");
    BOOST_CHECK_EQUAL(requests[5].path, "/branches/42/commit");
    const UniValue commit_request = ParseBody(requests[5].body);
    BOOST_CHECK_EQUAL(commit_request["bng_height"].getInt<int>(), 1);
    BOOST_CHECK_EQUAL(commit_request["bng_block_hash"].get_str(), header.GetHash().ToString());
    BOOST_CHECK(!server.SawUnexpectedRequest());
}

BOOST_AUTO_TEST_CASE(solve_header_rejects_unavailable_ctam_server_without_fallback)
{
    std::string unavailable_url;
    {
        MockCtamServer server{{}};
        unavailable_url = server.Url();
    }

    CBlockHeader header = Header();
    uint64_t max_tries{100};
    ArgsManager args;
    args.ForceSetArg("-ctammining", "1");
    args.ForceSetArg("-ctamresturl", unavailable_url);
    args.ForceSetArg("-ctamtimeoutms", "1");
    BOOST_CHECK_THROW(node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE), std::runtime_error);
    BOOST_CHECK_EQUAL(header.nNonce, 0U);
    BOOST_CHECK_EQUAL(max_tries, 100U);
}

BOOST_AUTO_TEST_CASE(verify_header_short_circuits_when_ctam_status_matches_next_tip)
{
    CBlockHeader header = Header();
    MockCtamServer server{{
        {EVHTTP_REQ_GET, "/status", StatusJson(/*bng_height=*/1, /*bng_block_hash=*/header.GetHash().ToString())},
    }};

    ArgsManager args;
    ConfigureArgs(args, server);
    BOOST_CHECK(node::CtamMiningBackend{args}.VerifyHeader(header, /*expected_trunk_height=*/0, uint256::ONE));

    const auto requests = server.Requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 1U);
    BOOST_CHECK_EQUAL(requests[0].path, "/status");
    BOOST_CHECK(!HasRequestPath(requests, "/crypto/cta_sha256"));
    BOOST_CHECK(!HasBranchCommitRequest(requests));
    BOOST_CHECK(!server.SawUnexpectedRequest());
}

BOOST_AUTO_TEST_CASE(mining_preflight_rejects_stale_ctam_trunk_without_starting_job)
{
    for (const auto& stale_status : {
             StatusJson(/*bng_height=*/1),
             StatusJson(/*bng_height=*/0, /*bng_block_hash=*/uint256::ZERO.ToString()),
             StatusJsonWithoutHeight(),
             StatusJsonWithoutBlockHash(),
         }) {
        MockCtamServer server{{
            {EVHTTP_REQ_GET, "/status", stale_status},
        }};

        CBlockHeader header = Header();
        uint64_t max_tries{100};
        ArgsManager args;
        ConfigureArgs(args, server);
        BOOST_CHECK_THROW(node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE), std::runtime_error);
        BOOST_CHECK_EQUAL(header.nNonce, 0U);
        BOOST_CHECK_EQUAL(max_tries, 100U);

        const auto requests = server.Requests();
        BOOST_REQUIRE_EQUAL(requests.size(), 1U);
        BOOST_CHECK_EQUAL(requests[0].path, "/status");
        BOOST_CHECK(!HasRequestPath(requests, "/mining"));
        BOOST_CHECK(!HasBranchCommitRequest(requests));
        BOOST_CHECK(!server.SawUnexpectedRequest());
    }
}

BOOST_AUTO_TEST_CASE(stopped_and_exhausted_jobs_do_not_commit)
{
    {
        MockCtamServer server{{
            {EVHTTP_REQ_GET, "/status", StatusJson()},
            {EVHTTP_REQ_POST, "/mining", JobJson("running", false, 42, false, "null", 0)},
            {EVHTTP_REQ_GET, "/mining/1", JobJson("stopped", false, 42, false, "null", 3)},
        }};
        CBlockHeader header = Header();
        uint64_t max_tries{100};
        ArgsManager args;
        ConfigureArgs(args, server);
        BOOST_CHECK(!node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE));
        BOOST_CHECK_EQUAL(header.nNonce, 0U);
        BOOST_CHECK_EQUAL(max_tries, 97U);
        BOOST_CHECK(!HasBranchCommitRequest(server.Requests()));
        BOOST_CHECK(!server.SawUnexpectedRequest());
    }

    {
        MockCtamServer server{{
            {EVHTTP_REQ_GET, "/status", StatusJson()},
            {EVHTTP_REQ_POST, "/mining", JobJson("running", false, 42, false, "null", 0)},
            {EVHTTP_REQ_GET, "/mining/1", JobJson("completed", false, 42, false, "null", 100)},
        }};
        CBlockHeader header = Header();
        uint64_t max_tries{100};
        ArgsManager args;
        ConfigureArgs(args, server);
        BOOST_CHECK(!node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE));
        BOOST_CHECK_EQUAL(header.nNonce, 0U);
        BOOST_CHECK_EQUAL(max_tries, 0U);
        BOOST_CHECK(!HasBranchCommitRequest(server.Requests()));
        BOOST_CHECK(!server.SawUnexpectedRequest());
    }
}

BOOST_AUTO_TEST_CASE(malformed_winning_job_is_rejected)
{
    MockCtamServer server{{
        {EVHTTP_REQ_GET, "/status", StatusJson()},
        {EVHTTP_REQ_POST, "/mining", JobJson("running", false, 42, false, "null", 0)},
        {EVHTTP_REQ_GET, "/mining/1", JobJson("completed", true, 42, false, "7", 8)},
    }};

    CBlockHeader header = Header();
    uint64_t max_tries{100};
    ArgsManager args;
    ConfigureArgs(args, server);
    BOOST_CHECK_THROW(node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE), std::runtime_error);
    BOOST_CHECK_EQUAL(header.nNonce, 0U);
    BOOST_CHECK(!HasBranchCommitRequest(server.Requests()));
}

BOOST_AUTO_TEST_CASE(winning_job_rechecks_ctam_trunk_before_replay_and_commit)
{
    for (const auto& stale_status : {
             StatusJson(/*bng_height=*/0, /*bng_block_hash=*/uint256::ZERO.ToString()),
             StatusJson(/*bng_height=*/0, /*bng_block_hash=*/uint256::ONE.ToString(), /*context_size=*/CTAM_CONTEXT_SIZE + 1),
         }) {
        MockCtamServer server{{
            {EVHTTP_REQ_GET, "/status", StatusJson()},
            {EVHTTP_REQ_POST, "/mining", JobJson("running", false, 42, false, "null", 0)},
            {EVHTTP_REQ_GET, "/mining/1", JobJson("completed", true, 42, true, "7", 8)},
            {EVHTTP_REQ_GET, "/status", stale_status},
        }};

        CBlockHeader header = Header();
        uint64_t max_tries{100};
        ArgsManager args;
        ConfigureArgs(args, server);
        BOOST_CHECK_THROW(node::CtamMiningBackend{args}.SolveHeader(m_interrupt, header, max_tries, /*expected_trunk_height=*/0, uint256::ONE), std::runtime_error);
        BOOST_CHECK_EQUAL(header.nNonce, 0U);

        const auto requests = server.Requests();
        BOOST_REQUIRE_EQUAL(requests.size(), 4U);
        BOOST_CHECK_EQUAL(requests[0].path, "/status");
        BOOST_CHECK_EQUAL(requests[1].path, "/mining");
        BOOST_CHECK_EQUAL(requests[2].path, "/mining/1");
        BOOST_CHECK_EQUAL(requests[3].path, "/status");
        BOOST_CHECK(!HasRequestPath(requests, "/crypto/cta_sha256"));
        BOOST_CHECK(!HasBranchCommitRequest(requests));
        BOOST_CHECK(!server.SawUnexpectedRequest());
    }
}

BOOST_AUTO_TEST_CASE(verify_header_uses_single_nonce_ctam_request_without_retaining_branch)
{
    MockCtamServer server{{
        {EVHTTP_REQ_GET, "/status", StatusJsonWithoutBngMetadata()},
        {EVHTTP_REQ_POST, "/crypto/cta_sha256", R"({"digest_hex":"0102030405060708090001020304050607080900010203040506070809000102","branch_key":9,"branch_kept":false,"injection_rounds_difficulty":16,"rounds":64})"},
    }};

    const CBlockHeader header = Header();
    ArgsManager args;
    ConfigureArgs(args, server);
    BOOST_CHECK(node::CtamMiningBackend{args}.VerifyHeader(header, /*expected_trunk_height=*/0, uint256::ONE));

    const auto requests = server.Requests();
    BOOST_REQUIRE_EQUAL(requests.size(), 2U);
    BOOST_CHECK_EQUAL(requests[1].path, "/crypto/cta_sha256");
    const UniValue verify_request = ParseBody(requests[1].body);
    BOOST_CHECK(verify_request.exists("payload_hex"));
    BOOST_CHECK(!verify_request.exists("nonce_start"));
    BOOST_CHECK(!verify_request.exists("nonce_end"));
    BOOST_REQUIRE(verify_request["keep_branch"].isBool());
    BOOST_CHECK(!verify_request["keep_branch"].get_bool());
    BOOST_CHECK(!server.SawUnexpectedRequest());
}

BOOST_AUTO_TEST_CASE(verify_header_rejects_stale_ctam_trunk_without_hashing)
{
    for (const auto& stale_status : {
             StatusJson(/*bng_height=*/1),
             StatusJson(/*bng_height=*/0, /*bng_block_hash=*/uint256::ZERO.ToString()),
             StatusJsonWithoutHeight(),
             StatusJsonWithoutBlockHash(),
         }) {
        MockCtamServer server{{
            {EVHTTP_REQ_GET, "/status", stale_status},
        }};

        const CBlockHeader header = Header();
        ArgsManager args;
        ConfigureArgs(args, server);
        BOOST_CHECK(!node::CtamMiningBackend{args}.VerifyHeader(header, /*expected_trunk_height=*/0, uint256::ONE));

        const auto requests = server.Requests();
        BOOST_REQUIRE_EQUAL(requests.size(), 1U);
        BOOST_CHECK_EQUAL(requests[0].path, "/status");
        BOOST_CHECK(!HasRequestPath(requests, "/crypto/cta_sha256"));
        BOOST_CHECK(!HasBranchCommitRequest(requests));
        BOOST_CHECK(!server.SawUnexpectedRequest());
    }
}

BOOST_AUTO_TEST_SUITE_END()
