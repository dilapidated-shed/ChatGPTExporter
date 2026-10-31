# Sun X3 candidate — retrieval and identity safety

User authorization: **Run X3** (2026-10-11 local). Control-plane envelope: [Flexible Pipes #86](https://github.com/isomorphisms/flexible-pipes/issues/86). This document is a proposed code receipt, not proof of registered FP execution.

Base: isolated X2 draft `4754aefcabbae45ff301145ac84d54dd7f2ef16c`, rather than changing main. The X2 suite and any original/adversarial failures remain tracked independently. This branch corrects D04, D07, D08 and D13:

- D04: callback checkpoint immediately after each successful batch/current conversation, with immutable batch response evidence associated with each completed unit. A later sibling failure cannot erase earlier completed units.
- D07: verify duplicate-message relative order and forbid a newly discovered older-page message appearing after already observed anchors. Apply the same check in X2 evidence replay.
- D08: reject conflicting `id` versus `conversation_id` at capture time. In a batch, recover only affected items via plural API; in singular and shared responses, fail closed.
- D13: reversible lowercase-hex hierarchical path for accepted exceptional IDs (long, uppercase, leading/trailing hyphens, reserved names); preserve ordinary lowercase legacy paths. Before any write, fail closed on a pre-existing ambiguous exceptional legacy directory. Existing files are not moved or deleted. A separately qualified explicit migration is required for old ambiguous layouts.

Synthetic regression tests include real capture engine persistence/resume and audit of the distinct paths; hosted exact-head checks must be observed separately.

Limits: some previously accepted upper-case/long-ID archives at old lossy paths require future explicit collision-safe migration; no silent migration is attempted. D05/D09/D10/D11 belong to subsequent X4–X6. Real provider, real browser/file-system, installed extension, FP stage dispatch and independent ai-ci acceptance are unverified. No merge/release.
