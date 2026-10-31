# Archive contract version 1

Raw input is authoritative. Canonical conversation schema 1 uses stable
workspace/conversation keys, every graph node and message, selected-branch
membership, typed content parts, metadata and retained provider extensions.
The browser and offline normalizer is `chatgpt-web-v2`. The C implementation
is independently identified as `chatgpt-native-c1`; reference comparisons cover
branching/inactive nodes, supported content classes, unknown structures,
metadata, citations, research and ISO-8601 timestamps. This does not establish
universal native/browser parity for all possible provider inputs. For example,
native citation URL handling conservatively supports ordinary HTTP(S) links;
it does not implement the browser's full WHATWG/IDNA URL canonicalizer.

Canonical workspace identity is the first 32 lowercase hex characters of
SHA-256(`chatgpt-web-workspace-v1` + NUL + UTF-8 account ID). The native raw
journal retains its original full SHA-256 account namespace for compatibility;
native canonical conversation keys use the shared identity above. Offline
imports retain their source-derived workspace identity and never imply live
discovery of another account or missing records.

## Completion states

All current producers emit `archiveContractVersion: 1` and one of these
`completionState` values. State is a derived report, never proof by itself.

| State | Meaning |
| --- | --- |
| `complete_for_verified_scope` | Required conversations and requested assets independently validate for the recorded observable scope. |
| `conversations_complete_assets_partial` | Retained conversation graphs validate; assets are incomplete, unavailable or not acquired. |
| `incomplete_or_inconclusive` | Inventory, raw evidence, derived views, identities, hashes or required coverage do not validate. |
| `offline_import_complete` | Every supplied conversation record validates against retained file evidence; no account-wide discovery claim. |

The browser preserves its historical `terminalState` field for compatibility.
Its completion state is in `reports/validation.json`; native uses
`native-index.json` and offline import uses `reports/offline-import.json`.
`fullAccountCaptureComplete` and `atomicProviderSnapshotProven` remain false.
An inventory traversal over time is never an atomic provider snapshot.

## Native disk validation and migration

Each `conversation-<ID-SHA256>.json` and `.md` has a corresponding immutable
object. `view-<ID-SHA256>.tsv` binds the derived hashes to the exact detail,
listing page, listing record and inventory hashes. Every receipt and index
is also retained as an immutable SHA-256 object before mutable publication.
`cgw-verify` reconstructs the latest two raw inventory traversals, validates
graphs and rederives every view/index before comparing disk bytes. Neither
exporter flags nor an edited report can confer acceptance.

Raw-only native archives from earlier candidates are incomplete under this
view contract. Capture/resume reuses verified raw graphs and creates the
missing views/index without deleting earlier raw objects. Corrupt immutable
objects are refused. Changed derived files can be rebuilt from valid raw.

Native storage remains a separate private disk envelope from the browser's
`conversations/`, `source/` and `indexes/` tree. Translation into the maintained
Agent Session Archive consumer and full native project/shared/asset parity
remain **unqualified**. No maintained `asm web-import` source/runtime identity
was available for actual consumer execution during this integration.
