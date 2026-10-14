# SUN X6 — semantic normalization and bounded graph validation

Original instructions: [October 7 Sun X6](https://github.com/dilapidated-shed/ChatGPTExporter/blob/sun/evidence-validation-resume-audit/docs/reviews/follow-up-jobs-2026-10-07.md). Captured user-authorized control-plane envelope: [Flexible Pipes #95](https://github.com/isomorphisms/flexible-pipes/issues/95). An issue, draft PR or green GitHub CI job is not a registered FP dispatch or ai-ci acceptance.

Exact ancestor: X5 draft `f7216e429c849cb7ced479076ae7656921a81128`. Main, X2, X3, X4 and X5 branches are untouched.

- D09: `conversation.json` schema normalizer `chatgpt-web-v2` excludes `__pagination_evidence` while preserving all semantic provider extensions, individual unknown message/role/part records and explicit coverage. All raw pages remain in append-preserving `source/detail-*.json` and must pass shared X2 replay before resume and independent audit. Pagination width, overlap or cursor layout does not change canonical JSON for the same supported message sequence.
- Legacy: independent audit can compare valid v1 derived output by dropping its embedded page evidence **only when it is byte-semantic-equal to the independently verified raw receipt**; unknown newer versions and inconsistent old receipts fail. Normal resume upgrades v1 derived files offline from raw via the version check rather than editing trusted source.
- D10: iterative completed/visiting-set graph walk visits each parent edge a bounded number of times and detects cycles and nonexistent links without recursion or a repeated ancestor scan for every node.
- Pagination acquisition and raw replay retain all X3 overlap-order checks, but accumulate reverse chronological page groups and assign stable order ordinals. They concatenate the ID array just once, avoiding quadratic repeated copying for long multi-page histories.

No new parser, renderer, dependencies, account credential flows, release or merge. Physical/browser/provider exhaustive export and credential safety boundaries remain separate acceptance work. Test results and exact CI receipts must be established from real runs, not inferred.
