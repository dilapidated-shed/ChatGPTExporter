# ChatGPT Web Probe

This branch is an observation instrument, not the exporter.

The app is deliberately small: a framework `android.app.NativeActivity` plus
one NDK-built C/JNI library. It contains no Java/Kotlin application source and
no `classes.dex`. The native entry point creates an Android WebView through JNI,
enables Web Contents debugging, and opens `https://chatgpt.com/`.

## Ownership

- Cat Food owns the A1-primary / C67-paired target facts.
- android-NDK owns generic NativeActivity APK construction.
- ai-ci owns build-toolchain and finished-APK producer/signing gates.
- Kitchen owns scripts served to people and scripts consumed by automation.
- Flexible Pipes owns the repeatable paired producer workflow.
- This repository owns only probe source, manifest and research capture scripts.

Do not add another Android build system here.

## Acceptance

After the Cat Food/Flexible Pipes artifact is installed and the probe is running:

```sh
rish -c 'grep -Ei "webview.*devtools_remote" /proc/net/unix'
```

A socket resembling `@webview_devtools_remote_<pid>` is the first required
runtime observation. Then use `scripts/connect.sh` and `scripts/capture.py`.

Raw fixtures are evidence. Types come later.
