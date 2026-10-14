# CLI command surface

The exact generated help text, including every flag, argument, output format and exit code, is mirrored verbatim at:

`mirror/chatgpt-cli/254982ab784e4d13c7ab5f87de1522f9b047d0f5/docs/reference/cli.md`

That file is generated from the binary's `--help` output and checked by `crates/cli/tests/cli_reference.rs`, so it is a better canonical command inventory than a hand-maintained list.

## Top-level commands

The mirrored CLI exposes:

- `configure` — provider-key status/store/remove.
- `sync` — incremental or full local-index synchronization.
- `list` — local conversation listing/filtering.
- `stats` — suggestions, brainstorm and topic counts, plus memory suggestions.
- `export` / alias `show` — render one conversation as Markdown.
- `search` — local full-text, semantic, hybrid, or remote ChatGPT search.
- `search-index` — build/refresh local search indexes.
- `daemon` — background service lifecycle.
- `archive` — archive selected conversations.
- `unarchive` — unarchive selected conversations.
- `delete` — delete selected conversations.
- `rename` — change ChatGPT's remote title.
- `title` — set a local-only display title.
- `titles` — generate local titles/topic themes.
- `classify` — Jev/Luna classification plus missing titles/themes.
- `project` — create/list/add/remove/delete project operations.
- `memory` — list/classify/summary/delete saved memories.
- `review` — one-by-one triage with final confirmation.
- `tui` — terminal UI.
- `help`.

## Nested commands

### `memory`

- `memory list`
- `memory classify`
- `memory summary`
- `memory delete`

### `project`

- `project create`
- `project list`
- `project add`
- `project remove`
- `project delete`

### `daemon`

- `daemon status`
- `daemon stop`
- `daemon logs`
- `daemon install`
- `daemon uninstall`
- `daemon run`

## Shared selection semantics

Where supported, commands share filters for age/date/title/archive state/limit/suggestion/topic/brainstorm classification. Explicit target commands accept full ids, unique prefixes, or `-` for ids on stdin. Repeated ids collapse to one target.

Bulk commands skip pinned chats unless `--pinned` is supplied. Dry-run/preview paths exist before destructive cleanup actions. The CLI separates stdout result data from stderr progress/prompts so pipelines remain usable.

## Output and failure contract

The generated reference records these exit classes:

- `0`: success.
- `2`: invalid arguments/input.
- `4`: browser session unavailable.
- `6`: ChatGPT rate limit still active.
- `7`: unsupported build/platform operation.
- `1`: other failure, including any failed item in a bulk action.

Where applicable, list/search/memory commands provide machine-readable formats such as JSON, CSV, table and ids.

## Why keep the generated reference

For FastChat/exporter work, use the generated CLI reference as an executable inventory of what a working client has already attempted. Do not manually retype flags into another “canonical” document and let the two drift.
