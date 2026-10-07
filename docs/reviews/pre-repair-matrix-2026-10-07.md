# Pre-repair failure matrix — 2026-10-07

Production revision: `274d878041403460a1249aa55b13d40769815bf2`. No production repairs made. Final harness: 288 tests, 247 pass, 41 fail. Original suite: 19 files / 94 tests pass; typecheck, privacy, build pass on separate untouched checkout. Expanded suite: 25 files / 194 added tests (153 pass, 41 fail); typecheck and build pass.

## Suite counts

| File | Total | Pass | Fail |
|---|---:|---:|---:|
| tests/chatgpt/account-artifacts.test.ts | 3 | 3 | 0 |
| tests/chatgpt/asset-session.test.ts | 4 | 4 | 0 |
| tests/chatgpt/assets-adversarial.test.ts | 3 | 1 | 2 |
| tests/chatgpt/assets.test.ts | 5 | 5 | 0 |
| tests/chatgpt/audit.test.ts | 3 | 3 | 0 |
| tests/chatgpt/auth.test.ts | 3 | 3 | 0 |
| tests/chatgpt/capture-engine.test.ts | 9 | 9 | 0 |
| tests/chatgpt/capture.test.ts | 15 | 15 | 0 |
| tests/chatgpt/client.test.ts | 5 | 5 | 0 |
| tests/chatgpt/cursor-adversarial.test.ts | 45 | 45 | 0 |
| tests/chatgpt/endpoints.test.ts | 6 | 6 | 0 |
| tests/chatgpt/envelopes.test.ts | 10 | 10 | 0 |
| tests/chatgpt/inventory-adversarial.test.ts | 11 | 8 | 3 |
| tests/chatgpt/inventory.test.ts | 8 | 8 | 0 |
| tests/chatgpt/normalize.test.ts | 4 | 4 | 0 |
| tests/chatgpt/pagination-adversarial.test.ts | 75 | 67 | 8 |
| tests/core/capture-store.test.ts | 3 | 3 | 0 |
| tests/core/filesystem-stream.test.ts | 1 | 1 | 0 |
| tests/core/request-control.test.ts | 5 | 5 | 0 |
| tests/core/sha256-stream.test.ts | 2 | 2 | 0 |
| tests/extension/protocol.test.ts | 2 | 2 | 0 |
| tests/integration/evidence-adversarial.test.ts | 60 | 32 | 28 |
| tests/integration/full-export.test.ts | 2 | 2 | 0 |
| tests/integration/resume-boundaries.test.ts | 4 | 4 | 0 |

## Every remaining failure

All rows below are actual implementation defects or mismatches with the stated completeness contract. D05 moving-list status is a claim/contract defect: snapshot isolation is not achievable from the available evidence. D13 covers locally accepted identifiers, not a claim that the provider issues these identifiers. D09 is derived JSON determinism/provenance contamination, not lost message text.

| # | Finding | Test | Observed result |
|---|---|---|---|
| 1 | D13 | X1 asset and archive identity isolation accepted long conversation ids cannot collide on archive path truncation | AssertionError: expected 'conversations/aaaaaaaaaaaaaaaaaaaaaaa…' not to be 'conversations/aaaaaaaaaaaaaaaaaaaaaaa…' // Object.is equality |
| 2 | D13 | X1 asset and archive identity isolation accepted leading-hyphen conversation ids cannot collide after sanitizing | AssertionError: expected 'conversations/conversation' not to be 'conversations/conversation' // Object.is equality |
| 3 | D05 | X1 inventory movement and terminal evidence does not certify a count hint while a full page can have unseen successors | AssertionError: expected [ 'a', 'b' ] to include 'c' |
| 4 | D05 | X1 inventory movement and terminal evidence does not label a demonstrated moving offset listing as proven complete | AssertionError: expected true to be false // Object.is equality |
| 5 | D06 | X1 inventory movement and terminal evidence audit detects missing authoritative inventory pages | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 6 | D07 | X1 pagination completeness rejects contradictory order even when overlapping message bytes agree | Error: promise resolved "{ inventory: { …(8) }, …(6) }" instead of rejecting |
| 7 | D08 | X1 fallback request order rejects conflicting conversation identity aliases | Error: promise resolved "{ inventory: { …(8) }, …(6) }" instead of rejecting |
| 8 | D08 | X1 fallback request order does not accept a batch record with contradictory identity aliases | AssertionError: expected 'batch' to be 'current' // Object.is equality |
| 9 | D09 | X1 determinism and provider-field preservation canonical JSON invariant at width 4 overlap 0 | AssertionError: expected { schemaVersion: 1, …(15) } to deeply equal { schemaVersion: 1, …(15) } |
| 10 | D09 | X1 determinism and provider-field preservation canonical JSON invariant at width 1 overlap 0 | AssertionError: expected { schemaVersion: 1, …(15) } to deeply equal { schemaVersion: 1, …(15) } |
| 11 | D09 | X1 determinism and provider-field preservation canonical JSON invariant at width 3 overlap 1 | AssertionError: expected { schemaVersion: 1, …(15) } to deeply equal { schemaVersion: 1, …(15) } |
| 12 | D09 | X1 determinism and provider-field preservation canonical JSON invariant at width 2 overlap 2 | AssertionError: expected { schemaVersion: 1, …(15) } to deeply equal { schemaVersion: 1, …(15) } |
| 13 | D10 | X1 determinism and provider-field preservation graph validation visits parent edges linearly | AssertionError: expected 80999 to be less than 4000 |
| 14 | D02 | X1 structural completeness independent of hashes resume rejects wrong response identity after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 15 | D02 | X1 structural completeness independent of hashes resume rejects wrong request identity after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 16 | D02 | X1 structural completeness independent of hashes resume rejects wrong operation after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 17 | D02 | X1 structural completeness independent of hashes resume rejects missing provider messages after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 18 | D02 | X1 structural completeness independent of hashes resume rejects mapping contradicts retained content after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 19 | D02 | X1 structural completeness independent of hashes resume rejects source tag launders incomplete plural evidence after recomputing hashes | AssertionError: expected { batchHash: null, …(11) } to be undefined |
| 20 | D01 | X1 structural completeness independent of hashes audit rejects remove first page after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 21 | D01 | X1 structural completeness independent of hashes audit rejects remove middle page after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 22 | D01 | X1 structural completeness independent of hashes audit rejects remove last page after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 23 | D01 | X1 structural completeness independent of hashes audit rejects reorder pages after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 24 | D01 | X1 structural completeness independent of hashes audit rejects duplicate page after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 25 | D01 | X1 structural completeness independent of hashes audit rejects alter before after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 26 | D01 | X1 structural completeness independent of hashes audit rejects alter start_cursor after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 27 | D01 | X1 structural completeness independent of hashes audit rejects alter page_count after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 28 | D01 | X1 structural completeness independent of hashes audit rejects append after terminal after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 29 | D01 | X1 structural completeness independent of hashes audit rejects complete flag with unfinished tail after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 30 | D01 | X1 structural completeness independent of hashes audit rejects current source without evidence after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 31 | D01 | X1 structural completeness independent of hashes audit rejects wrong response identity after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 32 | D01 | X1 structural completeness independent of hashes audit rejects wrong request identity after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 33 | D01 | X1 structural completeness independent of hashes audit rejects wrong operation after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 34 | D01 | X1 structural completeness independent of hashes audit rejects missing provider messages after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 35 | D01 | X1 structural completeness independent of hashes audit rejects mapping contradicts retained content after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 36 | D01 | X1 structural completeness independent of hashes audit rejects source tag launders incomplete plural evidence after recomputing hashes | AssertionError: expected 'complete' to be 'incomplete' // Object.is equality |
| 37 | D03 | X1 structural completeness independent of hashes current-branch archive must expose graph/version coverage | AssertionError: expected '{"report":{"schemaVersion":1,"provide…' to match /current.branch\|branch.coverage\|graph…/i |
| 38 | D02 | X1 interruption and historical compatibility legacy source label does not grandfather a disconnected graph | AssertionError: expected { …(12) } to be undefined |
| 39 | D11 | X1 historical assets and isolation resume repairs missing physical assets instead of trusting index hashes | AssertionError: expected +0 to be 1 // Object.is equality |
| 40 | D12 | X1 historical assets and isolation audit detects asset references removed from the index with refreshed hashes | AssertionError: expected 'complete' not to be 'complete' // Object.is equality |
| 41 | D04 | X1 historical assets and isolation a fully captured earlier record survives a later failure in the same batch | AssertionError: expected false to be true // Object.is equality |

## Harness corrections and non-defect classifications

First exploratory run: 278 total, 236 pass, 42 fail. Eight failures were harness defects: three max-cursor cases mistakenly used upstream proposed 4096 rather than current fork 2048; one expected the wrong missing-identity error code; three passed trailing slashes to listPaths; one injected an asset failure at writeBytesAtomic instead of the actual writeByteChunksAtomic boundary. Correcting those preserved the underlying safety assertions. The inventory-page deletion test then exposed a real audit failure. Ten further cases added six failures and four passes, producing the final counts above.

The initial ledger also mistakenly used the upstream PR's 4096 bound; corrected to executable fork policy 2048. No frozen source changed.

No valid test was skipped, weakened, marked expected-failure, or converted to TODO. Undefined provider behavior is not guessed: Unicode tests assert local round trips only; null current-node and empty/overlap-only-page cases test documented local policy. A requirement for snapshot isolation or proof of unseen provider versions would be unsupported; these tests instead require honest scope labeling.

Privacy scanner: original checkout passes. Audit branch fails on three pre-existing synthetic cookie literals in the newly imported frozen CLI test mirror: http/tests.rs:12 and fake-chatgpt/src/lib.rs:25,28. These are synthetic fixture false positives, not credentials discovered in live traffic. The scanner and frozen files remain unchanged; the failure is retained, not waived.

Reproduce: `npm ci`, `npm run typecheck`, `npm test`, `npm run privacy:check`, `npm run build`. `npm run check` intentionally exits nonzero at the valid failing tests. Full JSON test receipts live in `receipts/`; they contain synthetic data only.
