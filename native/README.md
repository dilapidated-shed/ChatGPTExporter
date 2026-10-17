# Your Own Export — native read-only web-history seam

This directory is a **credential-free native C request planner**, not yet a
headless exporter or an authenticated ChatGPT client. It builds the current
allowlisted ChatGPT web-history requests from typed operations and refuses
unbounded, arbitrary, or destructive requests. In particular, the sole POST is
the bounded, **read-only** conversation-batch lookup.

The existing Chromium extension **remains usable and unchanged**. Its browser
page bridge currently supplies authenticated same-origin access, and the
TypeScript inventory, raw-evidence journal, asset retrieval and independent
archive audit remain authoritative. This is a tested portability seam for a
future native exporter and FastChat history-reading adapter, **not** a claim
that Chrome has been removed or that a real account has been exported.

## Test

    make -C native check

The tests contain only synthetic identifiers, compare exact paths and JSON,
reject hostile identifiers/cursors/scope/batches, and test that failures
publish no partial request. This source has no third-party runtime dependency.

## Composition contract

    FastChat or native archive coordinator
      -> typed CGW operation and arguments
      -> cgw_resolve (this library; no I/O)
      -> caller-owned fixed-origin HTTPS transport
      -> caller-owned authenticated session (optional, short-lived)
      -> raw response verification and append-preserving archive

- The caller **must** pin \`https://chatgpt.com\`, TLS verification, approved
  operation/method, per-operation response byte limit and actual read-only
  behavior. Do not accept caller-provided URLs, headers, or methods.
- Session acquisition remains **outside** this library. A public OpenAI API
  key does **not** authorize ChatGPT web-history access. Do not solicit,
  print, copy from browser profiles, or persist bearer tokens or cookies.
  A safe consented sign-in adapter is an unresolved prerequisite for
  browserless live capture. Browser-mediated login may remain necessary.
- Provider paths are private, undocumented contracts and may change. Treat
  an HTTP/schema drift as explicit failure and preserve private source
  evidence; no guessed fallback.
- FastChat's current Chat Completions/Responses streaming adapter has
  different semantics. It must not use this history adapter for sending
  prompts. If the native planner is reused there, pin the exact source
  revision and add consumer-side synthetic acceptance before deploying it.
- This layer has **no** network, filesystem, raw-response graph, integrity,
  device, personal-account, real archive, or production acceptance.
- Exact endpoint behavior must remain checked against
  \`src/chatgpt/endpoints.ts\`; do not silently let the two drift.

This slice is tracked by the Flexible Pipes work envelope
[isomorphisms/flexible-pipes#147](https://github.com/isomorphisms/flexible-pipes/issues/147).
That issue alone is not a dispatched FP job or independent ai-ci receipt.
No merge/release is authorized without actual FP/AICI acceptance.
