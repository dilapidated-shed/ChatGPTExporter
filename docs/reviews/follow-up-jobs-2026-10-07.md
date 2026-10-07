# Follow-up Sun jobs — 2026-10-07

Run X2 first to settle shared evidence validation; then reconcile X3–X6 against its exact result. Each job is bounded by finding IDs. These are repair jobs, not permission to merge the audit branch. Existing unresolved audit failures must remain visible.

## Sun X2 — Repair evidence validation, resume and independent audit

Repository: https://github.com/dilapidated-shed/ChatGPTExporter
Model: Sun, extra-high reasoning.

Fetch current main and star/exporter-contract-test-audit; inspect active work before creating a repair branch. Audit evidence: c2156ae2547018173019fe2e4805cba997569fe8; audited production: 274d878041403460a1249aa55b13d40769815bf2. Read docs/reviews/chatgpt-exporter-codebase-audit-2026-10-07.md, contract-ledger-2026-10-07.md and pre-repair-matrix-2026-10-07.md. Verify current state rather than assuming those revisions remain tips.

Repair D01, D02, D03, D06 and D12. Introduce one pure contract validator/reconstructor that checks page operations, conversation identities, cursor linkage, termination, message agreement and mapping consistency. Invoke it during resume and independently during audit; never trust a stored complete flag or source tag. Audit authoritative inventory files and asset-reference coverage against raw content, not merely listed hashes. Add explicit current-branch versus provider-graph coverage.

Migration invariants: valid legacy batch/singular archives remain usable; incomplete plural evidence cannot become legacy by changing a tag; derivatives remain rebuildable; unknown provider behavior stays unknown. Make the relevant evidence-adversarial and inventory-page audit failures pass. Coordinate inventory claim semantics with X4 and asset repair with X5. Keep the three frozen synthetic-cookie scanner findings explicit until narrowly handled without editing snapshots or blanket exclusions.

Preserve all original and adversarial tests, frozen mirrors and valid legacy archives. Run focused tests, then the full suite, typecheck, privacy check and build; distinguish repaired failures from remaining audit failures. Never skip or weaken a valid assertion. Do not merge, release or broadly rewrite. Report exact revisions, remaining unknowns and acceptance results directly in chat.

## Sun X3 — Repair retrieval identity, overlap order and checkpoints

Repository: https://github.com/dilapidated-shed/ChatGPTExporter
Model: Sun, extra-high reasoning.

Fetch current main and star/exporter-contract-test-audit; inspect active work before creating a repair branch. Audit evidence: c2156ae2547018173019fe2e4805cba997569fe8; audited production: 274d878041403460a1249aa55b13d40769815bf2. Read docs/reviews/chatgpt-exporter-codebase-audit-2026-10-07.md, contract-ledger-2026-10-07.md and pre-repair-matrix-2026-10-07.md. Verify current state rather than assuming those revisions remain tips.

Repair D04, D07, D08 and D13. Reject contradictory id/conversation_id aliases and overlap order. Persist each validated conversation before a later record in the same batch can fail. Make accepted conversation identifiers map injectively to archive paths; preserve existing unambiguous archives and detect migration collisions before writing.

Keep batch -> plural -> initial-404-only singular ordering. Never switch contracts after history pagination begins. Make the alias, contradictory-order, same-batch survival and path-collision tests pass. Extend integration coverage to prove two colliding old path names cannot overwrite one another. Keep incomplete units incomplete and successful units durable. No provider identifier grammar may be guessed to evade the path tests.

Preserve all original and adversarial tests, frozen mirrors and valid legacy archives. Run focused tests, then the full suite, typecheck, privacy check and build; distinguish repaired failures from remaining audit failures. Never skip or weaken a valid assertion. Do not merge, release or broadly rewrite. Report exact revisions, remaining unknowns and acceptance results directly in chat.

## Sun X4 — Repair inventory termination and uncertainty claims

Repository: https://github.com/dilapidated-shed/ChatGPTExporter
Model: Sun, extra-high reasoning.

Fetch current main and star/exporter-contract-test-audit; inspect active work before creating a repair branch. Audit evidence: c2156ae2547018173019fe2e4805cba997569fe8; audited production: 274d878041403460a1249aa55b13d40769815bf2. Read docs/reviews/chatgpt-exporter-codebase-audit-2026-10-07.md, contract-ledger-2026-10-07.md and pre-repair-matrix-2026-10-07.md. Verify current state rather than assuming those revisions remain tips.

Repair D05. Separate normally terminated traversal, retained observed IDs and evidence of account-wide completeness. Stop treating an unverified total as a sufficient completeness oracle. Handle movement, duplicates and main/archive/project/shared membership without dropping observed records. Reconcile prior absences conservatively.

Use dated CLI evidence and the moving-offset tests. Re-reading the top may reduce omissions but cannot establish snapshot isolation; document the precise limit. Update completion/report semantics to expose uncertainty instead of inventing a stable provider snapshot. Make count-hint and moving-inventory safety assertions pass without simply renaming the old overclaim. Coordinate authoritative inventory validation with X2. Preserve explicit terminal evidence and all existing inventory tests.

Preserve all original and adversarial tests, frozen mirrors and valid legacy archives. Run focused tests, then the full suite, typecheck, privacy check and build; distinguish repaired failures from remaining audit failures. Never skip or weaken a valid assertion. Do not merge, release or broadly rewrite. Report exact revisions, remaining unknowns and acceptance results directly in chat.

## Sun X5 — Repair physical-asset resume verification

Repository: https://github.com/dilapidated-shed/ChatGPTExporter
Model: Sun, extra-high reasoning.

Fetch current main and star/exporter-contract-test-audit; inspect active work before creating a repair branch. Audit evidence: c2156ae2547018173019fe2e4805cba997569fe8; audited production: 274d878041403460a1249aa55b13d40769815bf2. Read docs/reviews/chatgpt-exporter-codebase-audit-2026-10-07.md, contract-ledger-2026-10-07.md and pre-repair-matrix-2026-10-07.md. Verify current state rather than assuming those revisions remain tips.

Repair D11 and coordinate D12 with X2. Before skipping a completed conversation or project asset set, verify the referenced physical material, not only assets.json and marker hashes. Recover missing/corrupt assets while reusing validated conversation evidence; do not refetch complete conversations unnecessarily.

Make the missing-physical-assets resume test pass. Preserve oldest-page discovery, overlap/reference deduplication, project/conversation identity isolation and explicit partial status. Add corrupted-byte and project-resume cases at the same boundary. X2 owns independent raw-reference-versus-index audit validation; agree on the shared identity contract without duplicating competing validators. Report which retries require network and which rebuild locally.

Preserve all original and adversarial tests, frozen mirrors and valid legacy archives. Run focused tests, then the full suite, typecheck, privacy check and build; distinguish repaired failures from remaining audit failures. Never skip or weaken a valid assertion. Do not merge, release or broadly rewrite. Report exact revisions, remaining unknowns and acceptance results directly in chat.

## Sun X6 — Separate canonical output from page evidence and remove quadratic graph work

Repository: https://github.com/dilapidated-shed/ChatGPTExporter
Model: Sun, extra-high reasoning.

Fetch current main and star/exporter-contract-test-audit; inspect active work before creating a repair branch. Audit evidence: c2156ae2547018173019fe2e4805cba997569fe8; audited production: 274d878041403460a1249aa55b13d40769815bf2. Read docs/reviews/chatgpt-exporter-codebase-audit-2026-10-07.md, contract-ledger-2026-10-07.md and pre-repair-matrix-2026-10-07.md. Verify current state rather than assuming those revisions remain tips.

Repair D09 and D10. Keep pagination payloads authoritative in raw storage without copying page-boundary details into canonical conversation.json. Preserve every provider field needed for reconstruction, unknown message/content extensions and explicit coverage. Distinguish semantic metadata from raw-evidence hashes.

Replace repeated ancestor walks with bounded graph validation that still detects missing links and cycles; remove repeated accumulated-history copying where necessary. Make all four canonical-JSON comparisons and the parent-edge visit test pass while retaining Markdown, message ordering, nonempty asset-index equivalence, 2,000-message capture and page-limit tests. Use operation counts and bounded synthetic histories, not fragile timing thresholds. Coordinate validator ownership with X2; no unrelated renderer rewrite.

Preserve all original and adversarial tests, frozen mirrors and valid legacy archives. Run focused tests, then the full suite, typecheck, privacy check and build; distinguish repaired failures from remaining audit failures. Never skip or weaken a valid assertion. Do not merge, release or broadly rewrite. Report exact revisions, remaining unknowns and acceptance results directly in chat.
