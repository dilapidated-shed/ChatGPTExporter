# WebView → native durable capture integration (PR #15)

This is a draft source integration, **not** an authenticated or installed export.

## Existing source lineage

- PR #13: bounded inventory/detail capture, immutable content-addressed objects,
  journals, private ownership/modes, graph verification, restart revalidation.
- PR #14: verified first-party Android WebView, main-frame message port, short
  session callback, native C11 lifecycle, direct DEX callback classes.
- The Chromium extension is not changed by this integration.

The HTTPS-specific host capture adapter now lives in
`native/cgw_capture_https.c`. The source-preserving coordinator in
`native/cgw_capture.c` takes a bounded typed transport callback. SHA-256
is implemented in portable C11, with known-answer vectors (including a
million-byte, many-chunk test) checked in CI; no app link to Android's
private platform libcrypto is needed for this code.

## Trust boundary and archive handling

1. Only a committed `https://chatgpt.com` main document with an established
   authenticated WebMessagePort can execute an archive request.
2. Native selects `CGW_CONVERSATION_PAGE` or `CGW_CONVERSATION_DETAIL` and
   one validated 1–256-byte workspace identifier. Native serializes the
   workspace in a typed command; the page adds `ChatGPT-Account-Id` only to
   the corresponding fixed-origin request. No arbitrary method/path is allowed.
3. A page-local closure obtains access to the short-lived session token and
   sends it only in a fetch header from the WebView context. Credentials,
   cookies and tokens do not cross to the archive engine, filesystem, command
   arguments, journal or event log.
4. Unlike the E1 diagnostic probe (which sanitizes a response), archival
   requests preserve the **exact received UTF-8 JSON bytes**. Credential-like
   keys, token reflection or signed URLs fail the whole response instead of
   silently rewriting a supposedly immutable provider record.
5. Native validates nonce, request ID, navigation generation, exact chunk
   order, response cap and terminal status. The coordinator persists staging
   first; only successfully validated graph objects receive durable markers.
   Failure aborts staging, and no full-account completion claim is made.
6. A capture-specific `cgw_webview_capture_exchange_fn` is intentionally
   separated from the coordinator. It must run on a capture worker and
   serialize JNI/UI-thread port dispatch and callbacks. Without that working
   platform exchange the installed E1 app remains a **session probe only**.

## Tests and evidence boundaries

- `make -C native check-sha256` — known-answer vectors for portable hashing.
- `make -C native check-webview-capture` — offline page-port simulation
  with full private raw object capture, repeated inventories, restart reuse,
  expiry, revoked navigation, forged messages, truncation, incompleteness,
  and byte caps. This is synthetic host testing, **not Android execution**.
- `npm run check` — page-world exact raw preservation and secret refusal.
- `make -C android native` — NDK builds candidate native WebView libraries
  and separate capture libraries for arm64 and x86_64. These are compilation
  targets, not assertions about the physical tablet ABI.
- The E1 Android 14 emulator still exercises sign-in probe callbacks, not
  the end-to-end durable capture integration. The signed emulator fixture is
  not the user release package.

## Android TAB_P10 handoff

The active device is the SVITOO TAB_P10; A1/C67 have water damage and are not
acceptance targets. No physical tablet ABI, device API, private storage
permissions, filesystem durability, WebView sign-in, update identity or
mounted SD-card behavior is accepted by this PR. Obtain them from actual
Cat Food physical discovery rather than inference. A candidate arm64 library
does not establish that the tablet accepts an arm64 APK. Do not push or sign
an installation for an assumed ABI.

**Still blocked:** real platform worker/UI exchange, installed Android
archive and process-death recovery, live first-party sign-in and authorized
conversation retrieval, complete coverage of projects/shared conversations/
assets, production certificate/update continuity, source-pinned ICK/DEX
producer acceptance and independent Android runtime qualification.

Do not auto-merge, release, copy credentials, infer full provider snapshots,
or silently change archived raw payloads.
