# Sun X2 — evidence validation, resume and independent audit

Status: requested repairs complete; branch remains deliberately not merge-ready.
No merge or release performed. All acceptance below is synthetic host execution;
no live ChatGPT account, private traffic or credentials were used.

## Exact revisions and active work

- Current main fetched and rechecked: `274d878041403460a1249aa55b13d40769815bf2`.
- Requested audit evidence: `c2156ae2547018173019fe2e4805cba997569fe8`.
- Recovered local audit tip: `1f0b625e1fd0323558054074cbc06bbb3426dc01`.
- Final source/test revision checked: `5574627513638920c4951236b3a1b3088e8e6363`.
- Repair branch: `sun/evidence-validation-resume-audit`, based on the recovered
  audit tip, preserving its tests, documentation and receipts.

The requested `star/exporter-contract-test-audit` ref was absent remotely:
fetch reported “couldn't find remote ref” and both branch search and ls-remote
confirmed absence. Its clean local checkout retained the requested audit commit
and follow-up tip. Neither audit commit was assumed to be published.

Before creating the isolated repair branch, open work and remote refs were
inspected. Only [dilapidated-shed/ChatGPTExporter PR #1, “Document and mirror the
ChatGPT web API surface”](https://github.com/dilapidated-shed/ChatGPTExporter/pull/1)
was open. The documentation and earlier pagination-fix refs were retained.
There was no active remote evidence-repair branch to update.

## Repaired findings

| Finding | Behavior after repair |
|---|---|
| D01 | Audit independently reads raw files and invokes pure replay, checks marker identity and batch bytes, reconstructs normalized content, validates retained conversations, and counts only validated completions. Refreshed hashes cannot conceal structural contradictions. |
| D02 | Resume invokes the same pure replay before reusing raw evidence, then reconstructs normalized content and compares logical asset references before skipping derived rebuilding. Missing, malformed, conflicting or source-inconsistent evidence is rejected. |
| D03 | Normalized files, conversation import rows, JSON and Markdown audit reports explicitly distinguish `current_branch` from `returned_provider_graph`. Provider-wide graph and version coverage remain `unknown`. Legacy normalized files without coverage fields remain readable. |
| D06 | Capture/resume and audit read each declared authoritative inventory file, verify its content hash, replay cursor/offset linkage and terminal conditions, check summaries and observed conversation/project identities, listing hashes, memberships and project files. New inventories declare their receipt model. Legacy inventories without receipts are explicitly `legacy_unverified`. |
| D12 | Resume and audit rediscover references from raw message content and metadata, compare logical identity, message association, descriptors and inline bytes with asset records. Audit checks physical bytes and global asset import rows independently. Asset omission cannot be hidden by refreshed index hashes or `not_requested` flags. |

The pure conversation validator verifies operations, requested and returned
identity aliases, turn count, exact cursor linkage, bounded nonrepeating cursors,
terminal position, duplicate-message agreement, reconstructed mapping/current
node and graph edges. A plural evidence object cannot be relabeled as a legacy
source, even when its pagination happens to be complete. Shared graph identity
retains the existing distinction between share id and provider conversation id.

An omitted requested asset keeps conversation content valid but makes asset
acceptance partial. Asset-index damage does not misclassify intact text as graph
damage. Changed normalized text, roles or provider extensions still fail
independent raw reconstruction. Valid batch/singular legacy archives can skip
and rebuild derived files with the network forbidden.

## Acceptance evidence

All commands ran at source/test revision
`5574627513638920c4951236b3a1b3088e8e6363` after offline dependency installation
from the unchanged lockfile. Receipts are in `receipts/x2/`.

| Check | Result |
|---|---|
| Original suite | PASS: all 19 original files, 94/94 assertions |
| Assigned original adversarial/compatibility selection | PASS: 52/52 selected assertions; 19 unrelated assertions excluded only from this focused invocation |
| Added X2 tests | PASS: 51/51, included in the full run |
| Full suite | 339 assertions: 325 PASS, 14 FAIL, zero pending/skipped |
| Typecheck | PASS, exit 0 |
| Build | PASS, exit 0 |
| Privacy check | FAIL, exit 1: same three frozen synthetic-fixture findings below |
| Repair diff whitespace check | PASS |
| Frozen mirror and privacy scanner integrity | Byte-for-byte unchanged from audit commit |
| Build scripts/dependencies/workflows | Unchanged from production main |
| Live provider/browser acceptance | NOT RUN |
| Hosted workflow acceptance | NOT RUN |

The baseline had 247/288 passing and 41 failing assertions. Exactly 27 assigned
failures now pass. The 247 already passing assertions still pass, as do all 51
new X2 assertions. No test was removed, marked expected-failure or made TODO.
The original capture-store positive fixture was corrected from an identity-only
stub to a valid graph; its identity/listing/hash assertions are unchanged. A new
negative case explicitly rejects identity-only evidence.

The additional tests cover operations/turn counts, identity aliases, overlap
conflicts, duplicate page messages, mapping/current-node corruption, cursor
controls/bounds, source-tag laundering, absent retained conversations, normalized
text/role/provider-content mutations, legacy coverage omission, authoritative
empty pages, missing/corrupt/refreshed inventory bytes, fabricated summaries,
cursor linkage, project files/chains, asset omission/duplication/context/descriptor
mutations, inline content, status laundering, offline reference rebuilding and
independent import-index validation.

Privacy findings remain explicit in `receipts/x2/privacy.txt`:

- Frozen CLI `crates/chatgpt/src/http/tests.rs:12`.
- Frozen CLI `crates/fake-chatgpt/src/lib.rs:25` and `:28`.

All three are known synthetic cookie literals in the dated source mirror, not
captured credentials. The scanner was not weakened, no exclusion was added,
and the frozen fixture bytes were not edited. This is a failing check, not PASS.

## Remaining findings

| Finding | Still failing assertions | Boundary left unresolved |
|---|---:|---|
| D04 | 1 | Earlier records in a batch are not checkpointed before a later record fails. |
| D05 | 2 | Count-hint termination and moving offset inventories still overclaim discovery completeness. Replaying stored inventory does not repair its acquisition policy. |
| D07 | 1 | Stable overlapping message bytes can still conceal contradictory relative order. |
| D08 | 2 | Live acquisition still accepts conflicting identity aliases. Replay now rejects them, but acquisition/fallback repair is separate. |
| D09 | 4 | Canonical normalized JSON still contains pagination provenance and varies with page layout. |
| D10 | 1 | Existing graph validation still walks parent chains quadratically. |
| D11 | 1 | Resume still does not independently check missing physical asset bytes before skipping a completed unit. Audit does check those bytes. |
| D13 | 2 | Archive path mapping still permits long-id and leading-hyphen collisions. |

## Unknowns and limits

Provider-wide graph/version coverage, real plural schema/order/cohort behavior,
stable account snapshot completeness, shared identity association, archived
omissions, account artifacts, real asset redirects and File System Access crash
durability remain unverified. A `complete` audit is explicitly scoped to retained
returned-graph/current-branch content; it is not proof of an exhaustive provider
export. Legacy inventory receipt absence stays visibly unverified.

Hashes plus replay detect structural contradictions. They do not authenticate
evidence against someone able to rewrite every raw file and every derived record
consistently. No cryptographic provider provenance is claimed.
