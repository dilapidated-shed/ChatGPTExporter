# Native fixed-origin HTTPS retrieval candidate

The native API planner is in \`cgw_web.[ch]\` (draft PR #10). This
separate **optional** libcurl effect adapter converts its exact, read-only
operations into HTTPS requests to \`https://chatgpt.com\`. It is an incremental
candidate for **Your Own Export**, and potentially a future FastChat history
reader. It does not replace the working Chromium exporter.

## Build and test

    make -C native check
    make -C native check-https

The second target requires a C11 compiler, libcurl development headers,
pkg-config, OpenSSL CLI, Python 3 (test fixture only) and Bash (test harness
only). The runner creates a temporary self-signed certificate with the
\`chatgpt.com\` subject alternative name, binds an HTTPS server only to
\`127.0.0.1:18443\`, and tests a separately compiled
\`CGW_TEST_LOCAL_TLS\` binary. It cleans the private key and certificate.
**That macro is forbidden in shipping binaries**, since it substitutes a
test-only local connection target and ephemeral trust anchor. Normal source
always connects to the fixed real \`https://chatgpt.com\` origin with system
certificate validation. There are no real credentials or ChatGPT requests in
these tests.

## Boundaries

- API methods and paths originate only from the typed \`cgw_resolve\` allowlist.
  The only POST reads a bounded batch of conversations. No freeform URL, body,
  request headers, cookie jar, follow-redirect setting, provider write, or
  provider mutation is exposed by \`cgw_fetcher_execute\`.
- The optional caller-provided token callback returns a short-lived bearer
  value **once for each already-validated request**. Tokens are never
  obtained by this library, written to disk, printed, read from browser
  profiles/environment/configuration, or reused across requests. The
  two required headers currently match the page bridge:
  \`Authorization\` and \`X-Authorization\`. An optional selected workspace
  contributes \`ChatGPT-Account-Id\` after identifier validation.
- Token copies and libcurl request-header buffers are zeroed on exit, to the
  extent possible in ordinary C memory. This does **not** prove every copy
  inside libcurl/OpenSSL/kernel memory is zeroed. Use a separate trusted
  source of authenticated tokens with explicit user consent, not a stored
  long-lived secret. The ChatGPT web session may require cookies or other
  context; **live bearer-only support has not been proven**.
- TLS verification and hostname checking are mandatory; requests never
  follow a redirect, accept proxies inherited from the environment, or
  perform automatic retries. HTTP 401, 403, 429, other errors, oversized
  bodies, token reflection, and callback failures produce distinct results.
  Observations expose only status, received byte count, and retryability.
  Any retry must be explicitly authorized and journaled by a higher layer.
- The response is streamed in bounded chunks. For low-memory devices this
  avoids materializing an entire 100 MB graph in RAM. Sink callbacks **must**
  write to a reversible staging area, never a completed archive; on any
  failure the caller must discard staging. This is not the exporter journal
  or its independent archival-completeness auditor.
- An incremental matcher rejects body bytes that reflect the bearer token,
  even across network chunks. Earlier streamed bytes still require staged
  rollback on rejection. HTTP errors and redirects never stream bodies
  into the supplied sink.
- The curl transport is synchronous. Call on an owned worker thread; no UI
  thread, cancel event, streaming JSON parser, live history graph adapter,
  checkpoint journaling, asset download follow-up, or browserless login
  adapter is supplied by this slice.

## What remains unproven

A read-only HTTP transport is not a replacement for the Chromium extension.
The missing *authentication source* is a real constraint, not a coding
detail: OpenAI's public API keys do not give access to ChatGPT web history.
Only adopt browserless access after separately verifying a permitted,
consented authentication flow and actual full account semantics. The existing
exporter X2–X6/B1/offline stack stays separate and unmerged.

Host acceptance from GitHub Actions and this fixture does **not** certify
real-account export, original/archived/project coverage, asset retrieval,
physical ARM32/ARM64 builds, installation, or a completed FastChat session.
Independent registered Flexible Pipes dispatch and ai-ci remain required
before merging/releasing. See FP work envelope #147.
