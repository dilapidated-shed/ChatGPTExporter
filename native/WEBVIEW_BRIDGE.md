# Native WebView bridge contract

`cgw_webview_bridge.[ch]` is the C11 trust boundary for the candidate Android
WebView sign-in shell under `android/`. This library does not create a WebView, perform JNI calls, obtain
credentials, execute JavaScript, persist a response, or issue network I/O.
The separate native platform adapter and direct-DEX callback classes require
their own Android build and emulator evidence; see `android/TESTING.md`.

The bridge accepts typed operations only through `cgw_resolve`. It then emits
one ASCII command for an already verified document:

```text
CGW1;request;<32-lowercase-hex-nonce>;<request-id>;<GET|POST>;<relative-path>;<base64url-body>
```

The browser-side port may reply only with:

```text
CGW1;reply;<nonce>;<request-id>;<HTTP-status>;<chunk-sequence>;<final-0-or-1>;<base64url-payload>
```

Before a request, the platform adapter must call
`navigation_started`, `document_committed("https://chatgpt.com", 1)`, and
`port_ready` in that order for the current top-level document. A navigation
revokes the port and cancels an active request. The bridge also revokes the
old port on every main-frame document callback, so a missed lifecycle callback
cannot preserve authority.

The C layer generates the nonce from caller-supplied kernel-backed entropy;
it owns the request ID, chunk sequence, and response cap. It rejects an
unverified origin, iframe, malformed message, wrong nonce or ID, replayed or
out-of-order chunk, mixed success status, non-2xx body, oversized decoded
response, and sink failure. It passes only decoded bounded response chunks to
the caller's reversible staging sink. It never sees or reads cookies, bearer
tokens, browser profiles, JavaScript storage, or unrestricted URLs.

Run the synthetic host check with:

```sh
make -C native check-webview
```

This test proves the protocol boundary only. It does not build an APK, create
a WebView, prove Android sign-in, contact ChatGPT, or make a C67 device claim.
