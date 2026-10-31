# SUN X4: offset inventory termination, observations, and coverage

Work envelope: [Flexible Pipes #88](https://github.com/isomorphisms/flexible-pipes/issues/88). Stacked on X3 draft `7d23640347aac17cdb8f1e8c9078f684db18c9fc`; leave main, X2 and X3 unchanged. This is a source candidate, **not** a registered FP dispatch or independent ai-ci acceptance.

D05: `total` is a mutable provider hint, never a sole terminal condition for a full offset page. Continue until a terminal empty page or short page with a compatible hint. Keep every returned ID, across duplicates and scope membership changes. Overlap or changing total hints mark an `offsetInstability` defect; preserve raw receipts, return an observed inventory with `complete=false`, and disable normal capture. `discovery.traversalComplete` remains true when all requested listing chains terminated.

Even an apparently stable traversal has `discovery.providerSnapshot="unverified"`: no stable listing snapshot, comprehensive provider inventory, archived completeness, or absent-from-provider conclusions are inferred from an offset/total chain. A top re-read can reduce omissions but cannot prove snapshot isolation; this change does not invent an authenticated snapshot API or a hidden retry bound. Previous unobserved records are retained on subsequent traversals, including following movement-marked observations; they are **not deleted**.

Independent replay recomputes the offset consistency flag and short-page semantics from raw source pages; invalid metadata edits fail even with freshly calculated JSON hashes. Reports explicitly separate traversal, observed records, and snapshot completeness. Older total-terminated receipts remain readable under their historical contract without being upgraded to provider-wide completeness. The dashboard refuses confirmation of observed offset instability while permitting inspection/revalidation.

No forced migrations, authentication changes, new dependencies or broad X5/X6 work. New X4 tests and exact-head CI must be inspected before acceptance.
