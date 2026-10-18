# One bounded main/archived capture

`cgw_capture` composes the typed planner, fixed-origin TLS transport and private
immutable store. It has no live-account executable. Its only authorization
input is the existing caller-owned callback and one explicitly selected
workspace. The host entrypoint always starts a synthetic TLS provider; it
accepts no token, account, URL, method, cookie, credential file or environment
token. The Chromium extension and separate X2–X6/B1/offline branches remain
separate and unchanged.

Both scopes use offset pages, mandatory finite totals and echoed offsets/limits.
Every successful response is committed as raw bytes before extracting IDs.
Duplicate/conflicting/overlapping IDs, missing-conversation flags, inconsistent
totals, repeated offsets, malformed JSON (including duplicate keys), early empty
pages and ceilings remain INCONCLUSIVE. Every nonempty chain receives an extra
empty terminal probe. A complete second inventory must have identical raw page
identities. This demonstrates a bounded stable observed inventory; it cannot
prove an atomic provider snapshot, which the report states explicitly.

Each inventoried ID uses the bounded detail endpoint directly, the conservative
fallback when a batch cannot establish complete per-ID graphs. No batch result
is assumed complete. Detail pagination indicators remain INCONCLUSIVE. The
complete raw mapping, unknown fields, roles and content are retained. Decoded
IDs, message envelopes, reciprocal graph edges, parents, selected node and
cycles are checked without synthesizing messages or discarding alternate
branches. Raw metadata is never normalized into a replacement archive.

Private journals retain observed/expected IDs, inventory identity, request and
raw object hashes, failure/retry hints, completion and deterministic resume
decisions. Resume repeats inventory and rehashes/revalidates each reusable
marker. Invalid markers and corrupt objects remain incomplete. Missing remote
records never delete prior local objects. Markers use workspace/ID fingerprints
and are atomically published only after raw/hash/graph validation and fsync.
A torn journal tail is retained and refused, rather than repaired by guessing.
An orphan raw object may be safely refetched/deduplicated; it is not completion.

Defaults bound the inventory to 256 IDs, requests to 128, response staging to
1 MiB, received work to 32 MiB, graph nodes to 1024 and JSON/graph comparison
work to 4,000,000 operations. Configuration has hard upper limits. Only one
bounded response is in memory. Transport can observe one rejected cURL callback
beyond a byte cap; it records that observation and never commits it. Journal
bytes are bounded at 4 MiB. The parser caps depth at 64 and tokens at 32768.
The fixture reports its process peak RSS, including TLS/test overhead; this is
neither a handset measurement nor an account-size scaling claim.

`check-capture` makes genuine private on-disk captures, preserves a successful
one via `build/capture-success.path`, and separately attacks corruption and
interruption. Its fixture matches the independently committed raw JSON files.
The retained TypeScript envelope/graph implementation checks their semantic
IDs, selected path and alternate branch. Separately sourced ai-ci code hashes
and compares actual objects, markers, expected IDs, modes and report scope.
Premature-completion, silent-omission, invalid-marker and skipped-trigger
mutants are retained and must be rejected.

Host `cc` runs are explicitly host/loopback evidence. The maintained production
objects and Kitchen/Ike recipe select source-pinned ai-ci ICK producer
`fbe86e23d55cfec2000c08e61deea2a407fd7175`, qualified by its producer source;
consumer compilation is a separate required current-head gate. The pinned
shared build-toolchain contract is mandatory. No local-routing test build may
be packaged as production. Browserless authentication, projects, shared scopes,
assets, Android and physical A1/C67 acceptance are outside this slice.
Every result says `full_account_export_complete=false`.

The initial hostile corruption fixture attempted to write an immutable 0400
object and therefore had not corrupted anything. Its corrected fixture changes
only its own synthetic object's mode, writes the actual corrupt byte, restores
0400, then requires hash/marker refusal. A premature-completion mutant also
exposed an irrelevant kill-test wait; mutants now run the coupled semantic
checks, while the baseline separately performs actual kill/restart. Neither
failed fixture is evidence of a passing product boundary.
