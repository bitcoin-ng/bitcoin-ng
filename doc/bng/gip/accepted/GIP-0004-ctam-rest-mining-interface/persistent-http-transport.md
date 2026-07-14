# Persistent HTTP Transport for CTAM Mining

**Status**: proposed
**Created**: 2026-07-14

## Abstract

BNG should reuse a persistent HTTP connection while communicating with a CTAM
server, especially across the start, poll, verify, and commit requests of one
mining job. The REST resources and JSON messages remain unchanged. This is a
transport optimization: it removes the current deliberate TCP disconnect after
every request without replacing REST with WebSockets or another protocol.

## Motivation

The current `CtamRestClient::RequestJSON()` implementation creates a new
libevent event base and HTTP connection for each request and sends
`Connection: close`. A normal successful mining operation can make several
sequential requests:

1. read CTAM status;
2. start the mining job;
3. poll the job until it is terminal;
4. confirm that the CTAM trunk has not changed;
5. replay the winning header for verification; and
6. commit the retained winning branch.

Closing the connection after every response adds repeated TCP setup and
teardown, socket churn, and latency to this workflow. The cost is particularly
visible in tests that create many fixture blocks, although the same unnecessary
overhead exists in production mining.

CTAM still performs the mining. Persistent transport does not make BNG compute
proof of work; it makes BNG's control-plane conversation with CTAM cheaper.

## Terminology

This proposal uses **persistent HTTP connection** to mean multiple HTTP
request/response exchanges over a reusable TCP connection. For the current
libevent client and REST server, the initial target is HTTP/1.1 keep-alive.

This is not a proposal for:

- WebSockets;
- a permanently open mining response;
- server-sent events;
- streaming JSON; or
- changing the CTAM REST endpoint model.

Those mechanisms may be considered separately if polling itself becomes the
dominant cost. They are not required to stop reconnecting for every poll.

## Proposed Behaviour

### 1. Connection ownership

A `CtamRestClient` instance should own a reusable transport session containing
the libevent event base and HTTP connection. Requests made through that client
should use the existing connection while it remains healthy.

The mining backend should retain one client for the complete mining operation,
including status checks, job creation, polling, result replay, and branch
commit. Callers that perform a related sequence such as reset and replay should
likewise retain a client for that sequence.

### 2. HTTP persistence

The client should stop sending `Connection: close`. It may either rely on the
HTTP/1.1 default persistent-connection semantics or send
`Connection: keep-alive` explicitly when required for compatibility with CTAM.

CTAM should return responses with complete message framing, normally a valid
`Content-Length`, so that the connection can safely carry the next response.
CTAM may close an idle or overloaded connection at any time.

Persistence is an optimization, not a protocol precondition. Correctness must
not depend on a particular TCP connection surviving for the lifetime of a job.
The job ID and CTAM trunk metadata remain the durable application-level
identifiers.

### 3. Reconnection

If CTAM closes the connection between requests, BNG should reconnect and retry
an idempotent request once, subject to the configured timeout and interruption
state. Examples include:

- `GET /status`; and
- `GET /mining/{job_id}`.

BNG must not blindly retry a state-changing POST after an ambiguous transport
failure. The server might have processed the request before the connection was
lost. Recovery should instead use application state:

- after an ambiguous mining-start response, use an idempotency key or a
  client-generated job ID before automatic retry is enabled;
- after an ambiguous commit response, query CTAM status to determine whether
  the expected height and block hash were committed; and
- after an ambiguous stop response, poll the known job ID.

Until those recovery rules exist for a particular POST, its current failure
behaviour should be preserved.

### 4. Timeouts and liveness

Connection reuse must not turn the request timeout into an unlimited session
timeout. Each request retains its own deadline. Mining-job polling remains
interruptible, and cancellation should still issue the stop request when a job
has been created.

The implementation should discard and recreate the connection after protocol
errors, malformed or incomplete responses, connection resets, or request
timeouts where the connection state is uncertain.

An optional idle timeout may proactively retire an unused connection. This is
independent of the mining-job lifetime and should be longer than the ordinary
poll interval.

### 5. Concurrency

A transport session should not be shared concurrently unless the client gains
explicit request multiplexing and synchronization. The initial implementation
should serialize requests on one client or give each concurrent mining worker
its own client/session.

HTTP/2 is not required by this proposal. It can later provide multiplexing, but
HTTP/1.1 keep-alive already removes the reconnect-per-request behaviour.

## Expected Impact

Persistent HTTP removes repeated TCP handshakes and connection teardown from a
mining job. It should reduce socket churn and request latency, particularly for
frequent job polling and fixture-heavy tests.

It does not remove:

- CTAM computation time;
- the mining poll interval;
- JSON serialization and parsing;
- the number of REST operations; or
- CTAM trunk synchronization, replay, and commit work.

Consequently this change should improve the fixed transport overhead but should
not be presented as a complete solution for slow tests. Tests unrelated to CTAM
may still benefit from cheaper fixture construction or a dedicated test path.

## Implementation Outline

1. Move the event base and `evhttp_connection` from request-local variables into
   transport state owned by `CtamRestClient`.
2. remove the `Connection: close` request header;
3. make request completion return control without destroying healthy transport
   state;
4. invalidate transport state on errors that make connection reuse unsafe;
5. reconnect transparently for safe, idempotent requests;
6. preserve the existing response validation and per-request timeout rules; and
7. add connection lifecycle logging at the CTAM debug level.

The transport state may be hidden behind a private implementation object so
libevent types and ownership details do not spread through the public client
header.

## Test Plan

Coverage should demonstrate that:

- a status, mining start, multiple polls, replay, and commit can use one TCP
  connection;
- sequential requests do not carry stale response bodies or callbacks;
- a server-side close between two polls causes a reconnect and polling resumes
  using the same job ID;
- an interrupted mining job remains stoppable;
- request deadlines still apply independently on a reused connection;
- malformed and truncated responses cause the connection to be discarded;
- ambiguous POST failures are not automatically duplicated; and
- clients used by concurrent workers do not race on one HTTP/1.1 connection.

Performance measurements should report connection count and elapsed time for a
fixed number of fixture blocks before and after the change. Test difficulty and
CTAM server configuration must be held constant.

## Security Considerations

Persistent connections do not change the trust boundary: BNG must continue to
treat every CTAM response as untrusted and validate the returned proof and trunk
state.

Per-request response state must be reset completely to prevent data from one
response being interpreted as part of another. Limits on response size,
timeouts, and error handling remain necessary because a persistent peer can
otherwise retain client resources for longer.

The initial local-only deployment guidance in GIP-0004 remains unchanged.
Persistent plain HTTP does not provide authentication or confidentiality for a
remote CTAM deployment; such a deployment requires a separately specified
secure transport and authentication model.

## Open Questions

- Does the current CTAM REST server already keep HTTP/1.1 connections alive
  when the client omits `Connection: close`?
- Should the client use a client-generated mining job ID or a general
  idempotency key to make mining-start retries safe?
- Should one `CtamRestClient` persist for the node's lifetime, or should the
  mining backend create a session per logical mining operation?
- Which connection and retry metrics should be exposed beyond debug logging?

## Changelog

- 2026-07-14: initial proposal
