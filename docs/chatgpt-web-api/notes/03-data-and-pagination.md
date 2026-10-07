# Data shapes and pagination

## Legacy full conversation graph

The singular/full-tree contract represents a conversation as:

- `mapping`: node id → node object containing `message`, `parent`, `children`;
- `current_node`: the leaf currently selected in the web UI.

That graph can preserve branches and inactive nodes when the provider returns them.

The CLI renderer recognizes at least:

- plain text parts;
- voice/audio transcription parts;
- image asset pointers;
- generated-image tool messages;
- attachments in `message.metadata.attachments`;
- web citation markers plus `metadata.content_references`;
- Canvas/canmore create/update records.

It intentionally omits or treats specially reasoning/tool/search records in its human rendering; raw evidence remains the authoritative source when archival completeness matters.

## Plural paginated message contract

Upstream issue #5 reports a different response shape:

- flat `messages[]`;
- `page_info`;
- newest page first from `/backend-api/conversations/{id}`;
- older pages from `/backend-api/conversations/{id}/messages?before={cursor}`.

A complete reader should:

1. Record the raw first page before normalization.
2. Verify the returned conversation identity wherever the response provides it.
3. If `has_previous_page` is true, require a usable `start_cursor`.
4. Treat the cursor as opaque data; encode it for transport rather than imposing a guessed alphabet.
5. Reject a missing cursor while another page is claimed.
6. Reject a repeated cursor/cycle.
7. Fetch and record every older page.
8. Deduplicate stable message ids at page boundaries.
9. Preserve provider order deliberately; do not rely on accidental map/object ordering.
10. Mark the conversation complete only after the provider says no older page remains.

A bounded `num_turns` value is not proof of completeness.

## Branch/version limitation

A paginated current-branch message stream does not automatically prove that sibling edited/regenerated branches were returned. If `include_has_versions=true` exposes version metadata without the full sibling payload, preserve that metadata but do not synthesize unseen graph nodes.

This matters because the legacy exporter contract promises a complete provider graph. A compatibility adapter may need to distinguish:

- complete provider graph;
- complete current branch;
- partial/unknown branch coverage.

## Offset paging

Conversation and shared lists use offsets. Because active conversation ordering is by update time, the list can shift during a long crawl.

The CLI's defensive strategy is:

- dedupe by conversation id;
- page until normal termination;
- re-read the top after paging.

The exporter additionally records page evidence/hashes and detects repeated ordered pages and non-advancing offsets.

## Cursor paging

Projects and per-project conversation lists use cursors.

Correct invariants:

- cursor is opaque provider data;
- URL-encode it;
- remember already-seen cursor values;
- reject cycles/repetition;
- reject empty pages that claim another page when that violates the observed contract;
- record the terminating page.

The historical mirror's `CURSOR = /^[A-Za-z0-9._~-]{1,512}$/` added an undocumented character restriction. Fork main at `274d878` now uses opaque strings, bounded at 2048 UTF-16 code units and excluding ASCII controls. This bound is local policy, not a provider guarantee. Upstream issue #4 does not disclose its cursor; upstream PR #2 separately reports a 548-character cursor.

## Batch reads

The batch endpoint accepts no more than 10 ids in both implementations.

CLI observations:

- unknown ids can simply be absent;
- deleted chats can still appear after singular reads say deleted;
- timestamps and id field names differ from singular responses;
- legacy batch `update_time` can be stale/rounded.

Therefore batch is an optimization/evidence source, not a sole existence/completeness oracle.

## Raw-before-derived rule

Current implementation caveat: plural pages accumulate in memory and are persisted together after terminal retrieval; interruption midway does not leave a durable per-page checkpoint. See the current [provenance ledger](../../reviews/contract-ledger-2026-10-07.md).

For archive work, raw pages/responses should be append-preserving evidence. Normalized JSON, Markdown, indexes, SVM features and titles are rebuildable derivatives.

That separation is especially important while ChatGPT serves multiple response contracts.
