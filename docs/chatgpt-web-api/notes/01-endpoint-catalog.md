# Endpoint catalog

This is a consolidated catalog of every ChatGPT web endpoint present in the mirrored evidence. “Observed” means a source says it was exercised against ChatGPT. “Exporter contract” means the exporter has code for it, whether or not the current cohort still accepts it.

## Authentication and account discovery

| Method | Path | Evidence | Purpose |
|---|---|---|---|
| GET | `/api/auth/session` | CLI + exporter | Exchange browser session cookies for session JSON containing `accessToken`. |
| GET | `/backend-api/accounts/check/v4-2023-04-27` | exporter contract | Discover accessible accounts/workspaces. |

Authenticated backend calls use the browser cookies and `Authorization: Bearer <accessToken>`.

## Conversation inventory

| Method | Path | Evidence | Notes |
|---|---|---|---|
| GET | `/backend-api/conversations?offset&limit&order=updated&is_archived&hide_snorlax=false` | CLI observed | `limit=100` observed. `hide_snorlax=false` includes project chats. Page until a short page; `total` is not a reliable final count. |
| GET | `/backend-api/conversations?offset&limit&order=updated[&is_archived=true]` | exporter contract | Exporter separately inventories projects instead of relying on `hide_snorlax=false`. |
| GET | `/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0&limit=20&owned_only=false[&cursor=…]` | CLI observed | Project list. Cursor is opaque data and is URL-encoded. |
| GET | `/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0[&cursor=…]` | exporter contract | Same family, but exporter currently applies a restrictive cursor character whitelist. |
| GET | `/backend-api/gizmos/{projectId}/conversations?cursor=…` | exporter contract | Separate per-project conversation enumeration. |
| GET | `/backend-api/shared_conversations?order=updated&limit&offset` | exporter contract | Shared-conversation inventory. |
| GET | `/backend-api/share/{shareId}` | exporter contract | Shared-conversation detail. |

### Inventory behavior recorded by the CLI

- `order=created` returned 500.
- Ordering by `update_time` means concurrent updates can shift offset pages. Dedupe by id and re-read the top.
- The list's `total` behaves like “offset + returned items + one more may exist,” not a trustworthy archive total.
- Archiving/unarchiving was observed not to change `update_time`.
- Archived listings can omit chats that still exist. A completeness-oriented sync must verify drop-outs.

## Conversation body reads

### Contract A: full-tree reads

| Method | Path | Evidence | Notes |
|---|---|---|---|
| GET | `/backend-api/conversation/{id}` | CLI observed; exporter contract | Returns a `mapping` graph plus `current_node`; CLI records `default_model_slug` here. |
| POST | `/backend-api/conversations/batch` body `{"conversation_ids":[…]}` | CLI observed; exporter contract | At most 10 ids. Returns full trees in the CLI cohort. Unknown ids may be omitted. |

The batch response is not structurally identical to the single response: CLI observations record `id` instead of `conversation_id`, ISO timestamps instead of epoch seconds, and no `default_model_slug`. Some deleted chats can remain visible in batch after single reads return 404.

### Contract B: plural paginated reads

Reported in upstream issue #5 for a ChatGPT Business cohort:

| Method | Path | Purpose |
|---|---|---|
| GET | `/backend-api/conversations/{id}?include_has_versions=true&num_turns=<n>` | Initial/newest message page. |
| GET | `/backend-api/conversations/{id}/messages?before=<cursor>&include_has_versions=true&num_turns=<n>` | Older pages. |

The reported response uses flat `messages[]` plus `page_info`, not a legacy `mapping` graph. `num_turns` is a page-size hint, **not** a completeness proof. Continue while `page_info.has_previous_page` is true and advance with `page_info.start_cursor`.

The issue reports both legacy reads returning 404 in that cohort.

## Search

| Method | Path | Evidence | Notes |
|---|---|---|---|
| POST | `/backend-api/global/search` | CLI observed | One hit per matching message; conversation id under `payload.conversation_id`; observed maximum `limit` 40. |

## Conversation mutations

| Method | Path | Body | Evidence |
|---|---|---|---|
| PATCH | `/backend-api/conversation/{id}` | `{"is_archived":true|false}` | CLI observed |
| PATCH | `/backend-api/conversation/{id}` | `{"gizmo_id":"g-p-…"}` | CLI observed; move into project |
| PATCH | `/backend-api/conversation/{id}` | `{"gizmo_id":""}` | CLI observed; remove from project |
| POST | `/backend-api/conversation/id/{id}/rename` | `{"title":"…"}` | CLI observed |
| DELETE | `/backend-api/conversation/id/{id}` | none | CLI observed |

Older chats have returned 500 after rename/project mutations that nevertheless applied. The CLI verifies ambiguous project moves by reading the conversation. A delete is considered real when later reads return `404 conversation_deleted`.

## Projects

| Method | Path | Body | Evidence |
|---|---|---|---|
| POST | `/backend-api/projects` | `{"emoji":null,"instructions":"","memory_scope":"unset","name":…,"theme":null}` | CLI observed 2026-09-29 |
| DELETE | `/backend-api/gizmos/{id}` | none | CLI observed 2026-10-02 |

Project creation returns `resource.gizmo`; project deletion is counted only when the response contains `{"deleted":true}`.

## Saved memory

| Method | Path | Evidence | Notes |
|---|---|---|---|
| GET | `/backend-api/memories?include_memory_entries=true` | CLI + exporter | Returns `memories[]`. |
| POST | `/backend-api/memories/about_you/summary?source=personalization-setting` | CLI observed | JSON fallback used by CLI; returns titled sections. |
| DELETE | `/backend-api/memories/{memory_id}` | CLI observed | Success response contains `{"success":true}`. |
| POST | `/backend-api/memories/about_you/summary/stream` | UI observed by CLI author | Streamed UI summary; not the CLI's chosen path. |

The CLI document says a separate streamed summary-correction endpoint appears in the page bundle but was not exercised. The exact path is not present in the mirrored evidence; do not invent it.

## Other account artifacts used by the exporter

| Method | Path | Purpose |
|---|---|---|
| GET | `/backend-api/user_system_messages` | Custom instructions/system-message data. |
| GET | `/backend-api/settings` | Settings. |
| GET | `/backend-api/settings/beta_features` | Beta-feature settings. |

## File downloads used by the exporter

| Method | Path | Context |
|---|---|---|
| GET | `/backend-api/files/download/{fileId}?conversation_id={conversationId}&inline=false` | Conversation file. |
| GET | `/backend-api/files/download/{fileId}?gizmo_id={projectId}` | Project file. |

## Sending a new message

The mirrored CLI explicitly does **not** implement message sending. It records the current web flow as deliberately harder than read/mutation calls:

1. `POST /backend-api/sentinel/chat-requirements/prepare` with page-generated `{p}`.
2. Solve required proofs: proof-of-work, Turnstile VM program, and browser-fingerprint programs.
3. `POST …/chat-requirements/finalize` to obtain a short-lived requirements token.
4. `POST /backend-api/f/conversation/prepare` to obtain a `conduit_token`.
5. `POST /backend-api/f/conversation` with requirements/proof/Turnstile/conduit headers; response is server-sent events.

A direct post without the required proofs was observed returning 403 “Unusual activity…”. The CLI therefore treats a real visible browser as the practical route for web-chat sending.

## Status discipline

Do not collapse this table into “the current API.” The mirror records different live cohorts. Each endpoint should carry its source, observation date/cohort and fallback/completeness behavior.
