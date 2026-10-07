# ChatGPT Web Probe

A deliberately small Android research instrument for observing the private `chatgpt.com` web contract before designing the exporter.

This is **not** the exporter and should not acquire exporter architecture.

## Design

Chrome Stable on the C67 did not expose `chrome_devtools_remote` even with Android debugging enabled. Android WebView is Chromium too, but here we control the embedding app and explicitly enable Web Contents debugging.

The probe only:

1. hosts `https://chatgpt.com/` in a persistent WebView;
2. enables JavaScript, DOM storage and cookies so the user can log in;
3. calls `WebView.setWebContentsDebuggingEnabled(true)`;
4. leaves observation to Chrome DevTools Protocol through ADB.

There is deliberately no JavaScript-to-native bridge, conversation parser, normalization layer, or exporter storage model.

## Build

The repository workflow `.github/workflows/probe-apk.yml` builds a debug APK on the `probe` branch.

With Android SDK 34 and Gradle available:

```sh
gradle -p probe assembleDebug
```

APK:

```text
probe/app/build/outputs/apk/debug/app-debug.apk
```

The app is Java/WebView only, so one APK runs on both A1 and C67; there is no native ABI split.

## First acceptance test

Install and open **ChatGPT Web Probe**. The top bar shows the installed WebView package/version.

Then:

```sh
rish -c 'grep -Ei "webview.*devtools_remote" /proc/net/unix'
```

We want an abstract socket resembling:

```text
@webview_devtools_remote_12345
```

If no WebView DevTools socket appears, stop. The probe has failed its reason for existing.

## Forward DevTools into Termux

With Wireless Debugging/ADB connected:

```sh
cd probe/scripts
./connect.sh
```

The script discovers the WebView socket through Rish and asks ADB to forward it to local port 9222. It then prints `/json/list`.

Manual equivalent:

```sh
adb forward tcp:9222 localabstract:webview_devtools_remote_12345
curl http://127.0.0.1:9222/json/list
```

## Research order

Do not spider the whole account first. Collect deliberately chosen specimens:

1. session response;
2. active conversation-list page;
3. archived conversation-list page;
4. project list and an actual project cursor if pagination occurs;
5. one short conversation through both singular and plural read paths;
6. one very long conversation through the plural path and every older-page cursor;
7. a conversation with regenerated or edited branches;
8. a conversation with files/images;
9. a shared conversation;
10. memory/settings/custom-instruction endpoints already cataloged under `docs/chatgpt-web-api/`.

Record failures too. A 404, 403, 429, challenge, or structurally different response is evidence.

## Research rule

The fixture corpus is authoritative evidence. Types come later.

Do not turn one observed JSON shape into a universal invariant. Cursors are opaque provider data, and conversation-read capabilities may vary by account/workspace cohort.
