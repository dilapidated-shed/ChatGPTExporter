# Sun X5: resumable physical-asset qualification

- User-authorized scope: [Flexible Pipes #89](https://github.com/isomorphisms/flexible-pipes/issues/89).
- Exact parent: `ef932b45beecba9457512f7e245c378139c18234` (draft X4).
- D11: conversation and project resume must verify *actual* content-addressed bytes (streamed SHA-256, length, nonzero size, canonical path) before skipping on a stored completion marker.
- If retained raw conversation evidence passes X2 validation, rebuild derived output **without refetching conversation messages**. Already verified physical assets and matching descriptors are reused. Missing/corrupt physical assets are reconstructed from inline bytes or fetched with their original conversation/project authorization context.
- When a content-addressed destination exists but its bytes fail its own digest/size, staged newly hashed bytes replace it; the result is independently re-read before being marked complete. Failed repair remains explicit `partial`, and healthy raw evidence is unchanged.
- Previously accepted project asset indexes are checked for exact current descriptor matches and physical bytes. Project files never become a conversation-level file fetch.
- No arbitrary provider URL, token, new dependency, database, separate renderer, automatic main merge, or live account access.

Acceptance: synthetic regression fixtures, original suites, exact-head GitHub CI. Physical File System Access crash durability, provider-side asset immutability, independent ai-ci approval and registered Flexible Pipes delivery remain unverified.
