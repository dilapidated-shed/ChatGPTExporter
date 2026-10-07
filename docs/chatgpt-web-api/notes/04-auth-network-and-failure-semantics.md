# Authentication, network behavior and failure semantics

## Authentication model

The clients reuse a normal browser login.

1. Read the browser's ChatGPT cookies/session.
2. Request `GET /api/auth/session`.
3. Keep the returned access token in memory.
4. Send authenticated backend requests with both normal session context and `Authorization: Bearer <token>`.

The exporter avoids asking the user to paste tokens/cookies.

## Cloudflare / transport fingerprinting

The CLI's dated observations say ordinary HTTP clients can receive `403` with `cf-mitigated: challenge` because acceptance depends partly on TLS/HTTP fingerprinting.

Observed behavior changed even across nearby dates: some clients that failed earlier later succeeded, and fresh connections could change the outcome. The important rule is therefore not “library X always works,” but:

- identify challenge responses explicitly;
- retry on a fresh connection under a bounded policy;
- do not confuse a Cloudflare challenge with an API/schema error;
- prefer real-browser observation when reverse-engineering a changed endpoint.

## Rate limiting

Repeated singular conversation reads were observed hitting `429` after a few hundred requests, with no `retry-after`; the CLI author observed recovery after about a minute.

The legacy batch endpoint handled 100 chats in 43 seconds without 429 in that observation.

Implications:

- rate limiting belongs in the transport layer;
- retry state must survive long exports;
- do not treat 429 as “conversation missing”;
- batching can reduce request pressure where the cohort supports it.

## Ambiguous write outcomes

Some ChatGPT write endpoints can apply a mutation and still return 500.

Recorded examples:

- renaming older chats;
- moving older chats into/out of projects.

The CLI handles project-move 500s by re-reading the chat and comparing `gizmo_id`. That is the right model for non-idempotent/ambiguous responses: verify state before blindly repeating a write.

## Deletion

Conversation deletion is expected to make later reads return `404 conversation_deleted`.

Project deletion is accepted only when the response says `deleted: true`.

Memory deletion is accepted only when the response says `success: true`.

These operation-specific confirmations are stronger than treating any 2xx as proof.

## Read fallback rules

Current fork request order is batch first, then plural for missing/invalid/duplicate/suspicious records or batch 404, then singular only after an initial plural 404. The rules below constrain that plural lane; they do not describe plural as the first overall request. Challenge and rate-limit timings above are dated reports, not universal guarantees. Exporter 403 handling does not independently classify every Cloudflare challenge.

Fallback should be based on a narrowly classified incompatibility.

For the newer conversation-read report:

- try the plural paginated contract;
- if the **initial plural endpoint itself** returns a compatibility-style 404, a legacy singular read can be tried;
- do not silently fall back on auth failures, rate limits, malformed data, or a failure halfway through pagination.

Otherwise the exporter can transform an incomplete retrieval into a plausible-looking “successful” archive.

## Sensitive observation artifacts

HAR/network captures contain tokens, cookies and conversation data. The CLI documentation says to delete HARs after deriving the contract.

Repository evidence should instead keep:

- redacted request/response shapes;
- exact endpoint/method/query/body structure;
- observation date;
- workspace/account cohort type when relevant;
- synthetic fixtures;
- hashes/termination evidence.

Never commit live credentials or raw private account traffic.
