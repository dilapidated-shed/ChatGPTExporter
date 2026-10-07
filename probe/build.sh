#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$SDK" ]]; then
    echo "ANDROID_HOME or ANDROID_SDK_ROOT is required" >&2
    exit 1
fi

BUILD_TOOLS="${BUILD_TOOLS_VERSION:-34.0.0}"
TOOLS="$SDK/build-tools/$BUILD_TOOLS"
ANDROID_JAR="$SDK/platforms/android-34/android.jar"

for tool in aapt2 d8 zipalign apksigner; do
    [[ -x "$TOOLS/$tool" ]] || {
        echo "Missing $TOOLS/$tool" >&2
        exit 1
    }
done
[[ -f "$ANDROID_JAR" ]] || {
    echo "Missing $ANDROID_JAR" >&2
    exit 1
}

BUILD="$ROOT/build"
CLASSES="$BUILD/classes"
DEX="$BUILD/dex"
UNSIGNED="$BUILD/probe-unsigned.apk"
ALIGNED="$BUILD/probe-aligned.apk"
APK="$BUILD/chatgpt-web-probe-debug.apk"
KEYSTORE="$BUILD/debug.keystore"

rm -rf "$BUILD"
mkdir -p "$CLASSES" "$DEX"

javac \
    -source 17 \
    -target 17 \
    -classpath "$ANDROID_JAR" \
    -d "$CLASSES" \
    "$ROOT/src/org/isomorphisms/chatgptprobe/MainActivity.java"

"$TOOLS/aapt2" link \
    -I "$ANDROID_JAR" \
    --manifest "$ROOT/AndroidManifest.xml" \
    --min-sdk-version 26 \
    --target-sdk-version 34 \
    --version-code 1 \
    --version-name 0.1.0 \
    -o "$UNSIGNED"

mapfile -t CLASS_FILES < <(find "$CLASSES" -type f -name '*.class' -print)
"$TOOLS/d8" \
    --lib "$ANDROID_JAR" \
    --min-api 26 \
    --output "$DEX" \
    "${CLASS_FILES[@]}"

(
    cd "$DEX"
    zip -q -j "$UNSIGNED" classes.dex
)

"$TOOLS/zipalign" -f 4 "$UNSIGNED" "$ALIGNED"

keytool -genkeypair \
    -keystore "$KEYSTORE" \
    -storepass android \
    -keypass android \
    -alias androiddebugkey \
    -dname "CN=Android Debug,O=Android,C=US" \
    -keyalg RSA \
    -keysize 2048 \
    -validity 10000 \
    >/dev/null 2>&1

"$TOOLS/apksigner" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --out "$APK" \
    "$ALIGNED"

"$TOOLS/apksigner" verify --verbose "$APK"
echo "$APK"
