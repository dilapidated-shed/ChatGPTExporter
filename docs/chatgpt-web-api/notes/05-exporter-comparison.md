# Exporter versus CLI/evidence

This note compares the frozen exporter baseline `c5618b3` with the CLI/evidence mirror. It does not change exporter code.

## What the exporter already does well

The existing exporter is designed around archival evidence rather than a thin scraper:

- multiple workspace selection;
- separate main/archived/project/shared inventory scopes;
- page termination and duplicate/cycle checks;
- raw response preservation;
- batch + single fallback;
- graph validation;
- account artifacts;
- file retrieval;
- resumable capture/checkpointing;
- independent audit/validation.

Those guarantees are worth preserving even if the implementation is eventually replaced.

## Contract differences that matter

### 1. Conversation body API

Exporter baseline:

- `POST /backend-api/conversations/batch`
- fallback `GET /backend-api/conversation/{id}`
- parser expects a legacy mapping graph.

External Business-cohort evidence:

- both legacy endpoints return 404;
- `GET /backend-api/conversations/{id}?include_has_versions=true&num_turns=…`;
- older pages through `/messages?before=…`;
- flat `messages[]` + `page_info`.

So a URL substitution is insufficient. The parser, raw-page storage, resume state and completeness proof all need a paginated lane.

### 2. Project cursor handling

Exporter baseline validates project cursors with:

`^[A-Za-z0-9._~-]{1,512}$`

The CLI treats the cursor as an opaque string and form-encodes it.

Issue #4 reports a real Projects inventory failure exactly at cursor validation. The actual failing cursor is absent from the report, so the whitelist cannot be proven to be the cause, but the exporter has no evidence for that alphabet restriction. Opaque-cursor handling is the safer documented contract.

### 3. Project-list request

CLI observed:

`conversations_per_gizmo=0&limit=20&owned_only=false`

Exporter baseline sends only:

`conversations_per_gizmo=0`

plus cursor when present.

That may be accepted by some cohorts, but the mirror should retain the exact observed query rather than assume omitted defaults forever.

### 4. Main/project conversation inventory strategy

CLI uses `hide_snorlax=false` on the main conversation list to include project chats.

Exporter performs explicit project discovery and per-project conversation paging.

For an archival tool, the exporter strategy can provide stronger scope evidence; the CLI strategy is useful as a cross-check and fallback source of ids.

### 5. Missing account/API coverage in each project

The two projects cover different surface areas:

Exporter-only in the mirror:

- workspace account discovery;
- shared conversations;
- custom instructions/settings/beta settings;
- project/conversation file downloads;
- stronger archival completion evidence.

CLI-only or more developed:

- remote search;
- project creation/deletion/moves;
- archive/unarchive/delete/rename;
- saved-memory summary/delete;
- generated full CLI command contract;
- explicit Cloudflare/rate-limit/write-ambiguity observations.

Together they form a better reference than either codebase alone.

## Likely architecture direction

Before deciding whether to retain or file away the original exporter implementation, preserve its tests and evidence model.

A cleaner implementation can be organized as:

1. **Observed contract layer** — endpoint/method/query/body schemas keyed by capability/cohort.
2. **Transport/session layer** — browser session, access token, challenge/rate-limit classification.
3. **Inventory layer** — offset/cursor chains with explicit termination evidence.
4. **Conversation reader adapters**:
   - legacy full-tree;
   - plural paginated current-branch;
   - future contracts.
5. **Raw evidence store** — every page/response append-preserved.
6. **Normalization layer** — provider graph/current branch → one internal representation, with branch-coverage status.
7. **Derived products** — Markdown, search index, titles, SVM classification, FastChat filing.
8. **Audit layer** — proves what was requested, retrieved, missing and only partially knowable.

That lets the existing exporter become a reference implementation/test corpus if its current architecture is not worth extending, without losing the hard-won completeness checks.
