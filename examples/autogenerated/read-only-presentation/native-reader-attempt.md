# Native read-only archive reader

The user requested that verified normalized conversations be inspectable in
the same native presentation family as FastChat and IB. Existing C1 archive
verification and storage remain authoritative. This JNI adapter uses the
shared Material roles and real framework Buttons/TextViews, not another store.

Semantic sketch: `VerifiedArchive inventoryRevision → Conversation sourceRevision
→ SelectedParentPath | CanonicalBranchInventory → MessageIdentity → OrderedPart
→ GraphemePage`. A revision belongs to the worker request and immutable archive;
an Activity does not authorize acquisition. A stale UI delivery is discarded.
Close revokes the delivery capability before the detached worker finishes.

The existing Idriç app-owned callback metadata defines `onClick(View)` and the
actual listener interface. C is bounded to the existing C archive FFI, Android
framework JNI, and detached worker. An Idriç rewrite of JSON, storage or thread
ownership would duplicate established authorities and is not selected.

Read-only Intent action `org.isomorphisms.chatgptexporter.READ_SAVED` restores
the existing private workspace selection and creates no WebView, bridge,
transport or provider controls. The capture menu can temporarily project the
same archive while preserving its existing WebView/root. Opening is refused
while an archive operation is running. Close restores that root explicitly.

Verification, JSON decoding, branch traversal and Unicode page construction
run on the worker. At most 2048 UTF-16 units enter the UI per delivery. Android
ICU grapheme boundaries preserve combining characters/surrogates; an individual
oversized grapheme is explicitly refused. Parts remain ordered. Code is
monospaced; unknown/asset parts show their actual raw metadata, without claiming
asset acquisition. Android's standard selectable text supplies selection/copy.
All-branch order is labelled as inventory order. Source and inventory hashes
remain visible. Frontend cursors are ephemeral; reopen restores archive data,
not a prior viewport or live page.

Compilation uses ICK for the new renderer and C1 adapter, then NDK assembly/link.
Legacy capture/platform C retains its separately declared NDK qualification gap.
There is no spelling translation, Java/Kotlin/Compose/Gradle fallback.

Current attempt: source and checked callback integration authored; compilation,
real ART, independent negative controls, signed candidate and physical input
remain pending. Do not infer these from host projection acceptance.

Continuation: the C1 host/navigation/original-role checks and actual checked
Activity listener declaration compile. The first cold Android compiler jobs
spend their time rebuilding qualified ICK stages already available to IB.
The retained cold-build helper `android/retain-reader-compiler.grease` is a
deferred build path, not a production dependency. The selected build restores
IB's exact source-bound ai-ci82cc compiler artifacts for fbe86 ICK, verifies
their ZIP digests and source receipts, then runs the current independent NDK29
glyph/Bionic/atomics/complex/branch-protection/API25 rejection suite before
compiling application C. A previous NDK27 qualification is not relabelled NDK29.
No different compiler or source-spelling fallback is selected.

The explicit read-only entry also handles singleTask onNewIntent and retains
that entry across recreation. A reader opened from capture restores the actual
capture root only on explicit Close/Back. This is durable archive recovery,
not restoration of a destroyed live WebView or a prior viewport. Existing
256-byte workspace-selection initialization still belongs to the legacy
platform lifecycle; full removal of synchronous lifecycle storage is unqualified.

First functioning native build4aec passed reproducible ARM64 packaging and
actual API34 production-code viewing, source/role/branch/copy/compositor,
offline reopening, unchanged archive and corrupt normalized-source refusal.
At b2ef2af6 the stronger signed control-guard mutant exposed a main-thread
observer assertion escaping Instrumentation's result handler. Its crash was
not accepted as a negative pass. Independent ai-ci6cfacc59 relays the original
Throwable and verifies actual disk-derived source/inventory hashes. Run
41790878034 passes both API29 and API34 on unchanged b2 APKs, including the
precisely rejected actual signed mutant and corrupt-source case. The producer
now consumes that qualified observer. Footer controls gain an8dp gap so their
rounded surfaces and shadows do not collide. These new build bytes still need
their own source-bound qualification; no previous APK is relabelled.
