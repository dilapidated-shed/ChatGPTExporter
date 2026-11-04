# Read-only normalized archive presentation

The authoritative objects are the existing native archive, normalized messages,
ordered content parts and raw graph. A presentation view owns a bounded snapshot,
never a second conversation store or provider authority.

Input: private native archive root, selected workspace, conversation identity,
and existing capture bounds. Output: an opaque read-only view plus borrowed
message/part metadata and caller-bounded decoded text. Errors distinguish archive
verification, source identity, version, range, unsupported parts and capacity.

The existing C normalizer/parser and read-only archive verifier remain their
owners. This is a narrow C adapter to those actual interfaces; no replacement
Idriç JSON parser, universal serialization or alternative store is introduced.
Future checked Idriç consumers can bind this opaque snapshot through the existing
JNI/native-reference facility. That binding and Android UI integration are not
claimed by host execution.

All opening work belongs on a worker. Opening acquires the existing shared
workspace lock, independently reconstructs the archive, then verifies the exact
selected normalized bytes and immutable object before publishing a view.
No operation in this interface can acquire a network connection or send a prompt.

Tests use the exporter's explicitly synthetic offline capture corpus. They do
not constitute a user archive import or live-account acceptance.

The first adapter retained canonical all-branch inventory order but did not
provide conversational order. The existing fixture places the assistant before
the user in that inventory. The repaired interface caches a proved parent path
and exposes user then assistant without dropping the alternate unknown-role
branch. All graph traversal stays at worker-open, with constant-time cached
message-position lookup. Independent expectations reject a producer that returns
the path in reverse and one that reports an incompatible component version.
