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
