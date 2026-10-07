# ChatGPT web contract

ChatGPTExporter intentionally uses a small read-only subset of ChatGPT's private web API. These endpoints are not a public compatibility promise from OpenAI, so every operation is typed, bounded, synthetic-tested, and allowed to fail visibly when the web application changes.

## Typed operations

| Operation | Method and relative path | Purpose |
| --- | --- | --- |
| `session_probe` | `GET /api/auth/session` | Establish page-local authentication; the token is consumed only inside the page bridge. |
| `accounts_list` | `GET /backend-api/accounts/check/v4-2023-04-27` | Discover accessible personal/managed workspaces for explicit selection. |
| `conversation_page` | `GET /backend-api/conversations` | Enumerate main or archived history by bounded offset/limit. |
| `project_page` | `GET /backend-api/gizmos/snorlax/sidebar` | Enumerate projects and project-level file descriptors. |
| `project_conversation_page` | `GET /backend-api/gizmos/<id>/conversations` | Enumerate every project's conversation cursor chain. |
| `shared_page` | `GET /backend-api/shared_conversations` | Enumerate shared-conversation records. |
| `shared_detail` | `GET /backend-api/share/<id>` | Retrieve a share-only graph. |
| `conversation_batch` | `POST /backend-api/conversations/batch` | Retrieve at most ten exact conversation IDs. |
| `conversation_current` | `GET /backend-api/conversations/<id>?include_has_versions=true&num_turns=10` | Initial current-branch page after batch omission, invalidity, duplication, suspicious graph, or batch 404. |
| `conversation_messages` | `GET /backend-api/conversations/<id>/messages?before=<cursor>&include_has_versions=true&num_turns=10` | Follow older history until explicit terminal page_info. |
| `conversation_detail` | `GET /backend-api/conversation/<id>` | Legacy compatibility only after initial plural 404. |
| `account_artifact` | Four exact `GET` endpoints | Read memories, user system messages, settings, and beta-feature settings. |
| `asset_open` | `GET /backend-api/files/download/<id>` | Resolve a conversation- or project-scoped descriptor inside page world. |
| `asset_chunk` / `asset_close` | Page-local controls | Read bounded bytes through an opaque handle and close it. |

Every identifier, cursor, page size, batch size, body, method, and timeout is validated on both sides of the bridge. Callers cannot supply a URL, method, header, authorization value, query string, or arbitrary body. Archive/delete/rename/share/edit actions have no operation and cannot cross the protocol.

## Authentication and workspace selection

The page bridge calls the normal session endpoint with page cookies, caches the short-lived access token only in memory, and adds the provider-required authorization headers itself. The selected raw account ID is held in dashboard memory and becomes a stable one-way workspace fingerprint before any filesystem name or report is written. The extension never reads the active-workspace cookie.

`401` clears page-local authentication and produces `AUTHENTICATION_REQUIRED`; `403` produces `WORKSPACE_FORBIDDEN` without clearing it. The bridge does not independently distinguish a Cloudflare challenge from other 403 responses. `429` preserves bounded `Retry-After` metadata. Other HTTP errors expose a status, retryability, correlation ID, and response byte count without returning the private body.

## Evidence boundary (2026-10-07)

The plural lane proves termination of the returned current-branch stream under the local supported contract; it does not establish sibling/version completeness. Raw pagination evidence is persisted only after the complete fetch. Current source uses a 2048-code-unit cursor bound, excluding ASCII control characters; this is client policy, not observed provider grammar. Current inventory total-based termination does not establish snapshot completeness. Audit and resume have reproducible validation gaps; see the [audit report](reviews/chatgpt-exporter-codebase-audit-2026-10-07.md) before treating completion markers as proof.

## Endpoint evolution policy

When a live request drifts, preserve the private failed evidence, add the smallest sanitized synthetic fixture reproducing the shape, update the exact parser/descriptor, run every gate, and resume. Never broaden the bridge to arbitrary fetch as a workaround, and never edit completion markers or provider payloads by hand.
