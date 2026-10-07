# Sources and provenance

This directory is a documentation/evidence snapshot for the private ChatGPT web API used by the exporter.

These are **not** OpenAI's public API docs. The `chatgpt.com` web endpoints are private, unversioned implementation details and can change without notice.

## Pinned repositories

- Exporter fork: `dilapidated-shed/ChatGPTExporter`
  - historical mirrored baseline: `c5618b3cc06eeb5b273d3727fe8729071441f291`
  - audited fork main: `274d878041403460a1249aa55b13d40769815bf2`; see [current claim ledger](../reviews/contract-ledger-2026-10-07.md)
  - upstream: `siraht/ChatGPTExporter`
- CLI fork: `dilapidated-shed/chatgpt-cli`
  - mirrored commit: `254982ab784e4d13c7ab5f87de1522f9b047d0f5`
  - upstream: `planetaryescape/chatgpt-cli`

The mirror keeps the original repository paths beneath each pinned commit so every quotation or conclusion can be traced back to executable code, tests, generated command reference, or dated observations.

## External evidence

- `siraht/ChatGPTExporter#4` — Projects inventory fails with `project_page_cursor is invalid`; reports a successful non-Projects backup of 454 conversations with 0 failures.
- `siraht/ChatGPTExporter#5` — records a current cohort where legacy singular/batch conversation reads return 404 and the web app uses plural paginated reads:
  - `GET /backend-api/conversations/{id}?include_has_versions=true&num_turns=10`
  - `GET /backend-api/conversations/{id}/messages?before={cursor}&include_has_versions=true&num_turns=10`

## Observation dates in the CLI reference

The CLI's `docs/explanation/chatgpt-api.md` records observations from 2026-09-27 through 2026-10-02. Treat those dates as part of the evidence. A later web-app cohort can legitimately differ.

## Mirror policy

Files beneath `mirror/` are frozen evidence. Do not silently edit them to reflect later conclusions. Add new pinned mirrors or notes instead.

Files beneath `notes/` are our interpretation of the mirror and may be revised as evidence changes.
