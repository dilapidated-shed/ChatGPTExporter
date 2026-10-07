# ChatGPT web API reference

This directory separates raw evidence from interpretation.

The subject is the **private `chatgpt.com` web API**, not the public OpenAI API. It is unversioned and can vary by account/workspace cohort.

## Layout

- `SOURCES.md` — pinned revisions, observation dates and provenance.
- `mirror/chatgpt-cli/254982ab…/` — frozen copy of the CLI fork's complete documentation plus the HTTP/session/cookie implementation, tests, command definitions, fake API fixtures and mutation protocol.
- `mirror/chatgpt-exporter/c5618b3…/` — frozen historical exporter contract, endpoint resolver, parsers and capture tests, predating the fork's plural pagination lane.
- `mirror/external-evidence/` — frozen summaries of upstream issue evidence.
- `notes/` — our own reading of those sources.

## Read first

Read the [current provenance ledger and operation matrix](../reviews/contract-ledger-2026-10-07.md). Audited fork main is `274d878041403460a1249aa55b13d40769815bf2`; historical comparisons do not describe its present limitations.

1. `notes/01-endpoint-catalog.md`
2. `notes/02-command-surface.md`
3. `notes/03-data-and-pagination.md`
4. `notes/04-auth-network-and-failure-semantics.md`
5. `notes/05-exporter-comparison.md`

## Key conclusion

Do not assume one universal current ChatGPT web contract.

Evidence in the mirror shows at least two conversation-read contracts in active use during September 2026:

- a singular full-tree read at `GET /backend-api/conversation/{id}`, observed by the CLI;
- a plural paginated read at `GET /backend-api/conversations/{id}` with older-page retrieval through `/messages?before=…`, reported by a ChatGPT Business user.

Those observations overlap in time. Compatibility therefore needs capability detection/fallback and completeness checks, not a hard-coded chronological API version.
