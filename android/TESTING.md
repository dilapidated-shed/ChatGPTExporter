# E1 Android platform boundary

The source uses three direct DEX callback classes from android-NDK, native C11
for the Activity/WebView lifecycle, and a static page transport. It introduces
no Java, Kotlin, Gradle or TypeScript application layer. The existing native
planner exclusively chooses read-only operations. The action-bar menu exposes
one explicit session check; no real request starts automatically.

The production shell opens chatgpt.com for interactive first-party sign-in.
First-party auth.openai.com navigation is allowed without a native port.
Other cross-origin and federated login navigation is refused with a distinct result;
successful provider login is not established. TLS errors always cancel. File
and content access, mixed content, unsolicited windows and backup are disabled.
Safe Browsing remains enabled. Session credentials stay in the page closure;
replies remove credential fields and reflected tokens. Native logs contain
only fixed states and HTTP status numbers. E1 discards the bounded sanitized
probe response; archival capture belongs to E2/E3.

The port is created by native code, transferred only to the exact HTTPS
origin, and bound to the current main-document WebView instance. The injected
receiver runs only in the top frame. A random bootstrap handshake precedes
requests; callbacks from a revoked port cannot re-enable a later document.
Navigation closes the old port. The C protocol independently checks request
nonce, ID, byte limits, ordering and terminal state.

## Separate synthetic artifact

`CGW_WEBVIEW_SYNTHETIC` compiles a DIFFERENT test payload. It intercepts every
WebView request and automatically probes synthetic signed-in, signed-out,
expired HTTP 401, oversize and schema-drift fixtures. Production contains no
intent-controlled network substitution and never auto-probes.

The emulator-only package is `org.isomorphisms.chatgptexporter.e1fixture`.
It uses the existing public test certificate from Wegert revision
`dfa3751f0282cdf6a2083c97063b0e8e996d0d92`, certificate SHA-256
`de9b1d47c5a65e6d46a204b79dd9ee566b9d3c9832ba81ebc4213d3392e92ff9`,
matching android-NDK's maintained packaging fixture. No key is generated.
This is an explicit disposable emulator identity, not a production signer
selection or update-continuity claim. The arm64 production artifact stays
unsigned; E4 owns its stable identity, signing and delivery.

Host VM tests exercise the static page transport. Actual Android messaging
requires the separate emulator run. Neither proves physical C67 sign-in,
authenticated history coverage, export, resume or independent ai-ci acceptance.

The native build explicitly uses NDK 29.0.14206865. The shared build-policy
record retains the unqualified ICK/JNI gap; these are NDK candidate builds,
not proof of an ICK-produced or independently accepted Android toolchain.
