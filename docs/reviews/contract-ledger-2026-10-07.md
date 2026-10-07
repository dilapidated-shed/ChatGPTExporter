# Contract provenance ledger — 2026-10-07

Recorded before adversarial tests were authored. No live ChatGPT account was exercised. Source access is not live provider verification.

## Revisions fetched

| Repository/ref | Exact revision |
|---|---|
| dilapidated-shed/ChatGPTExporter main (production baseline) | 274d878041403460a1249aa55b13d40769815bf2 |
| docs/chatgpt-web-api-reference | ccca15d86418d708ec61f719feacee883a8a3d1e |
| siraht/ChatGPTExporter main | c5618b3cc06eeb5b273d3727fe8729071441f291 |
| dilapidated-shed/chatgpt-cli main | 254982ab784e4d13c7ab5f87de1522f9b047d0f5 |
| planetaryescape/chatgpt-cli main | 254982ab784e4d13c7ab5f87de1522f9b047d0f5 |

The documentation subtree is copied from its pinned ref; frozen mirrors are unchanged. Production remains based on fork main. The requested documentation SHA f15c072 is historical, not tip.

## Evidence classes

C = directly in current executable code; H = pinned historical implementation; T = exercised by synthetic test; O = dated external report, not independently observed here; I = inference; U = unknown. A test proves behavior against a fixture, not that the provider implements the fixture.

Exporter paths below are relative to src/chatgpt unless stated otherwise. CLI source is pinned above: crates/daemon/src/api.rs, api/writes.rs, crates/chatgpt/src/http.rs and session.rs. Dated provider claims are in CLI docs/explanation/chatgpt-api.md (September 27–October 2), not authenticated trace evidence supplied to this audit.

## Exporter operation matrix

| Operation | Documented contract | Current implementation | Existing tests | Strength | Known uncertainty |
|---|---|---|---|---|---|
| session_probe | GET /api/auth/session | auth.ts; token remains page-local; Authorization and X-Authorization; cookies include; safe metadata only | auth, client, protocol | C/T, CLI O | Expiry and cohort behavior not re-observed |
| accounts_list | GET /backend-api/accounts/check/v4-2023-04-27 | endpoints/client/envelopes; account dedupe, workspace fingerprints | client/envelopes | C/T/H | Accessible-account completeness U |
| conversation_page main | GET /backend-api/conversations?offset&limit&order=updated | endpoints omits hide_snorlax=false; inventory ends on declared total or empty page | inventory | C/T; CLI O for different query | Total is a hint in CLI report; no snapshot isolation or top reread |
| conversation_page archived | Same plus is_archived=true | separate offset chain, same count termination | inventory | C/T; CLI O | Archived omission reported; unseen chats cannot be recovered from counts |
| project_page | GET /backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0[&cursor] | endpoints omits CLI limit=20&owned_only=false; inventory cursor termination | endpoints/inventory | C/T; CLI O | Defaults U; provider cursor grammar U |
| project_conversation_page | GET /backend-api/gizmos/{id}/conversations?cursor | starts cursor=0; null/missing cursor terminates; empty continuation rejected | inventory | C/T/H | Exhaustiveness and missing-vs-null semantics U |
| shared_page | GET /backend-api/shared_conversations?order=updated&limit&offset | offset/total termination, owned id or share_ synthetic id | inventory | C/T/H | Total semantics and shared identity mapping U |
| shared_detail | GET /backend-api/share/{shareId} | graph checked, provider conversation id not bound to share id | capture | C/T/H | Share id need not equal conversation id; authoritative association U |
| conversation_batch | POST /backend-api/conversations/batch {conversation_ids}; max 10 | first choice; absent/invalid/duplicate/suspicious record -> plural; batch HTTP 404 -> plural | capture | C/T, CLI O | Structurally valid graph cannot prove unseen branches absent; deleted records may persist |
| conversation_current | GET /backend-api/conversations/{id}?include_has_versions=true&num_turns=10 | capture.ts validates initial identity and page_info, retains pages, linearizes branch | capture | C/T, issue 5 O | Ordering, null current node, version payloads U; no complete graph proof |
| conversation_messages | GET /backend-api/conversations/{id}/messages?before&include_has_versions=true&num_turns=10 | opaque cursor; up to 10,000 pages; stable duplicates reconciled; terminal false required | capture/endpoints | C/T; issue 5 reported compatibility description | No live schema sample; empty/overlap behavior and identity omission U |
| conversation_detail | GET /backend-api/conversation/{id} | only initial plural 404 -> singular; graph/id validation | capture/envelopes | C/T, CLI O | No fallback after history failure; graph completeness limited to response |
| account_artifact memories | GET /backend-api/memories?include_memory_entries=true | account-artifacts.ts, redacted derived snapshot | account-artifacts | C/T, CLI O | Completeness of memory/account export U |
| account_artifact custom_instructions | GET /backend-api/user_system_messages | same capture abstraction | account-artifacts | C/T/H | Current provider support U |
| account_artifact settings | GET /backend-api/settings | same capture abstraction | account-artifacts | C/T/H | Current provider support U |
| account_artifact beta_features | GET /backend-api/settings/beta_features | same capture abstraction | account-artifacts | C/T/H | Current provider support U |
| asset_open conversation | GET /backend-api/files/download/{fileId}?conversation_id&inline=false | page-local URL handle; asset-session.ts validates HTTPS host | assets/asset-session | C/T/H | Descriptor variants/redirect chain U |
| asset_open project | Same with gizmo_id | exactly one context required | assets/endpoints | C/T/H | Provider file identity across contexts U |
| asset_chunk | Page-local control, not provider endpoint | ranged GET via handle, max 1 MiB/chunk | asset-session/filesystem-stream | C/T | Real host Range compliance U |
| asset_close | Page-local control | delete handle | asset-session | C/T | No remote close contract claimed |

## CLI-only claims checked

Search POST /backend-api/global/search, limit 40: C in api.rs + O in CLI notes. Not exporter support.

Archive/unarchive PATCH /conversation/{id} with is_archived; move PATCH with gizmo_id (empty string removes); rename POST /conversation/id/{id}/rename with title; delete DELETE /conversation/id/{id}: C in writes.rs + O. Ambiguous 500-after-write behavior is O; state confirmation is C. A generic 404 is not a universal deletion proof.

Project creation POST /projects with emoji/instructions/memory_scope/name/theme, deletion DELETE /gizmos/{id} requiring deleted:true; saved-memory summary POST /memories/about_you/summary?source=personalization-setting and delete DELETE /memories/{id} requiring success:true: C/O. Streamed memory summary is O only. Unnamed summary-correction endpoint remains U.

Sending prepare/finalize/proof/conduit flow is O only, not implemented CLI/exporter support. The historical source's claim that a visible browser is the only workable route is not established universally. No sending or mutation was performed.

CLI command list and flags: C in crates/cli/src/args.rs; generated reference test present, not executed here. Do not call its current build independently validated.

429 after hundreds of singular reads, approximately one minute recovery, and batch timing: O only, not a rate-limit guarantee. Challenge rates and transport fingerprint explanations: O; causal explanation I, not controlled proof. CLI retry policy C. Exporter retries retryable transport errors; a 403 is not automatically a challenge classification. Auth/session behavior C/T, not a login guarantee.

## External issue/PR inspection

Open collections read 2026-10-07. Both CLI repositories had no open issues/PRs. Exporter fork has [PR #1, Document and mirror the ChatGPT web API surface](https://github.com/dilapidated-shed/ChatGPTExporter/pull/1).

- [Upstream issue #4](https://github.com/siraht/ChatGPTExporter/issues/4), September 3: Edu/SSO project cursor failure; no offending cursor disclosed.
- [Upstream issue #5](https://github.com/siraht/ChatGPTExporter/issues/5), September 16: Business plural 200 vs legacy 404 report. Pagination advice in report is not an independently captured complete multi-page trace.
- [siraht/ChatGPTExporter PR #1, Fix project inventory cursors and Windows path failures](https://github.com/siraht/ChatGPTExporter/pull/1): proposed cursor/path/body-limit/progress/failure-isolation changes, not merged evidence.
- [siraht/ChatGPTExporter PR #2, fix(chatgpt): accept real-world project pagination cursors](https://github.com/siraht/ChatGPTExporter/pull/2): reports 548-character cursor and successful 740-conversation export. Does not reveal the issue #4 cursor or establish universal grammar.
- [siraht/ChatGPTExporter PR #3, feat(extension): continuous capture progress counts and progress bar](https://github.com/siraht/ChatGPTExporter/pull/3): reports misleading 0/0 progress; source still has this callback.

## Documentation corrections before test design

The old mirror is historical c5618b3, not current fork code. Current cursor acceptance is 1–2048 UTF-16 code units excluding ASCII controls, encoded by URLSearchParams; this is local policy, not verified provider grammar. Current batch -> plural -> initial-404-only singular order supersedes the old batch -> singular comparison. Raw plural pages are retained only after the whole fetch completes, not durably after each network page. Complete current-branch retrieval does not establish complete provider graph/version retrieval. Inventory chains terminating does not prove a stable complete account snapshot. Strong archival/audit guarantees in old interpretive notes are design intentions pending adversarial evidence.

## Is the contract accurate enough?

Enough to test fail-closed behavior against the supported response shapes and to identify contradictions with dated reports. Not enough to certify complete provider export: no authoritative private API specification, live pagination/version trace, stable listing snapshot, or complete asset/account catalog was supplied. Unknowns remain unknown; synthetic tests below target client guarantees, not invented provider obligations.
