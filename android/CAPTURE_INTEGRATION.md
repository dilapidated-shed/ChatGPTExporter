# WebView → native durable capture integration (PR #15)

This is a draft native application integration. Installed synthetic qualification
and live/physical acceptance are recorded separately; no release is accepted.

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
6. `android/cgw_capture_android.inc` now connects that exchange to a pthread
   capture worker. A direct-DEX native TimerTask callback is posted through
   Android Handler; only its UI-thread callback touches the WebMessagePort.
   One mutex serializes bridge operations and a condition variable carries
   completion, navigation, cancellation and deadlines back to the worker.
7. Native controls expose session/account discovery, explicit workspace
   selection, capture/resume, cooperative pause, cancellation and disk
   revalidation. Private files are selected through `getFilesDir`, with a
   separate archive per workspace fingerprint. No cookie/token file is used.
8. The folder-copy action uses Android's normal document-tree picker. It
   validates private source evidence, creates a new destination directory,
   copies files without overwriting an earlier archive, then reopens and
   hashes each destination file. SAF behavior still requires installed tests;
   it does not inherit success from C compilation.

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
- The Android 14 synthetic fixture includes native worker capture and a
  force-stop/relaunch that must rehash and reuse the saved conversation.
  Its direct-DEX generator is application-owned Idriç metadata using the
  retained android-NDK encoder. Test networking remains intercepted entirely.
- `cgw_capture_revalidate` and the standalone C `cgw-verify` command reopen
  the latest journal and reconstruct both raw inventory traversals, expected
  identities, graph topology and object hashes. Exporter RESULT/validation
  flags do not establish acceptance. The verifier never repairs files.
- A capture encountering a torn journal first preserves all old bytes as a
  SHA-256 object, then atomically publishes the valid prefix and a recovery
  receipt. Synthetic host tests independently check the retained bytes/hash.

## Android TAB_P10 handoff

The active device is the SVITOO TAB_P10; A1/C67 have water damage and are not
acceptance targets. No physical tablet ABI, device API, private storage
permissions, filesystem durability, WebView sign-in, update identity or
mounted SD-card behavior is accepted by this PR. Obtain them from actual
Cat Food physical discovery rather than inference. A candidate arm64 library
does not establish that the tablet accepts an arm64 APK. Do not push or sign
an installation for an assumed ABI.

Native C now emits graph-preserving canonical JSON, readable Markdown,
hash-bound view receipts and a machine-readable index. The native verifier
rebuilds these from raw listing/detail evidence and rejects changed views,
indexes or receipts. See [archive contract](../docs/ARCHIVE_CONTRACT.md) for
reference coverage, layout differences and the migration boundary.

**Remaining implementation/qualification:** installed SAF acceptance,
universal canonical/layout parity, native projects/shared/assets acquisition, live sign-in/provider
calibration, physical tablet storage and lifecycle behavior, production
certificate/update continuity and current-source ICK shipping qualification.
The candidate currently captures bounded main/archived graphs and their views.

The ARM64 development candidate reuses the historical E1 development
certificate; it is not a production signer or a declaration of tablet
installation/update compatibility. The production package name remains
`org.isomorphisms.chatgptexporter`, version code 4. No signing key is generated.

Do not auto-merge, release, copy credentials, infer full provider snapshots,
or silently change archived raw payloads.
