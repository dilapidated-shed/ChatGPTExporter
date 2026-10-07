# Test model recorded before test implementation

Central invariant: a success marker must not outclaim its retained evidence.

1. Inventory chains require terminal evidence, distinct progress, lossless opaque cursors and union membership. Counts and unstable offsets cannot prove snapshot completeness. Previously observed ids remain retained; unobserved moved ids remain uncertain.
2. Plural capture requires initial identity, consistent supplied identities, boolean termination and an acyclic cursor chain. Only initial plural 404 permits singular fallback. Other HTTP/schema failures remain failures. Consistent overlaps may be reconciled; conflicts in content/metadata/author/time/assets/order must fail closed.
3. Equivalent logical histories must yield equal canonical meaning, ordering, Markdown and asset references regardless of page split; evidence provenance may differ. Unknown provider fields remain in authoritative raw and compatibility extensions.
4. Resume requires semantic validation as well as hashes. Reordered/truncated/forged chains, wrong identities, and mapping/evidence disagreement cannot count as complete. Legacy full graphs remain resumable when their original proof remains sufficient; changing a source tag must not launder plural data.
5. A completed conversation survives unrelated failures, including within a batch. Unfinished page chains never receive completion markers. Durable raw permits network-free derived rebuilding.
6. Assets on every history page are discovered; overlap cannot duplicate logical references. Multiple references may share physical bytes. Failed writes are recoverable; context identities stay separate.
7. Independent audit rechecks authoritative raw, listing, graph, index and completion evidence. Current-branch completion must be labeled separately from provider-graph completion. Hashes detect accidental byte changes; they are not signatures or completeness proofs.
8. Request retries preserve the exact operation/cursor; concurrent requests cannot mix identities. Large histories must expose accidental repeated copying and graph traversals; use operation counts where possible, not fragile microsecond thresholds.
9. Raw evidence contains provider payloads, not transport authentication/session metadata. Deliberate unknown-field preservation does not authorize copying transport secrets. A synthetic secret-in-provider-payload test, if used, tests defense in depth, not a claimed live occurrence.

Policy/unknown distinction: empty intermediate pages with fresh cursors and overlap-only pages are not demonstrated provider errors. They may continue within bounds but cannot themselves prove progress. Null current_node behavior and full version coverage remain unknown. Unicode cursor round trips test local acceptance, not provider issuance. Metadata detailHash is provenance and is expected to differ across page layouts; semantic fields must agree.
