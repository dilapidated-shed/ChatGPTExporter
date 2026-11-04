# Android exporter platform callback declarations

The existing Android shell needs a UI-thread runnable and an Activity result
callback for user-selected Storage Access Framework output. C11 continues to
own capture, state transitions, storage and lifecycle decisions.

Values: class descriptors, superclass descriptors, native library identity and
typed callback declarations. `emit : String -> String -> String -> List
NativeCallback -> IO ()` writes checked DEX bytes. The pure encoder is the
existing `Backend.DEX.EncodeCallbackShell.encode_callback_shell` in
android-NDK, pinned to the previously qualified producer
`950fef409148e91fc3ed59b0fbf64368f1ad0438`.

The UI task subclasses framework TimerTask, whose inherited Runnable interface
allows Handler.post; its run method is native. The Activity result callback
has two Int32 values and a nullable Intent reference. No Java, Kotlin, Gradle,
assembler or generated C produces these DEX classes. All native methods have
no DEX implementation body. The fourth class is application-owned metadata,
using the maintained generic encoder rather than another DEX writer.

The native reader adds a real `onClick(View)` callback and implements the
framework `View.OnClickListener` interface through the shared checked writer
at `b2ee19121aa3fa1112cd8338b7b6a7baca237b5f`. The other three retained
callback plans use its compatibility facade. This revision replaces the old
950f producer pin; it is not a second writer. The frontend is the FastChat-
qualified Idriç revision `ff4d852862a3942592f8ade9afde8d409d9803be`.

Acceptance: compile the actual Idriç source, emit twice and compare bytes,
independently inspect all DEX units, then exercise Handler and Activity callbacks
in installed Android. A source declaration or host structural check alone
does not establish runtime or physical acceptance. No fallback selected.
