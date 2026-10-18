# E1 build evidence only. Production signing and delivery belong to E4.
# All toolchain locations are explicit inputs from the verified build host.
CC_ARM64 ?=
CC_X86_64 ?=
BUILD_TOOLS ?=
ANDROID_JAR ?=
DEX_DIR ?=
KEYSTORE ?=
CERT_SHA256 ?=
NATIVE = ../native/cgw_web.c ../native/cgw_webview_bridge.c cgw_webview_android.c
FLAGS = -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared -Wl,-z,max-page-size=16384 -I../native

.PHONY: build native fixture validate verify-grease
native: build/lib/arm64-v8a/libcgw_webview.so build/fixture/lib/x86_64/libcgw_webview.so
verify-grease:
	printf '%s  %s\n' '7e31cd05b7a9d8fb2a4a9e003a7f3fcb0159138506d17f0fb28da8cbe22aa85c' '$(GREASE)' | sha256sum -c -
	chmod +x "$(GREASE)"
	ASAN_OPTIONS=detect_leaks=0 "$(GREASE)" -n qualify.grease > /dev/null
build: build/arm64-unsigned.apk build/fixture-unsigned.apk

build/lib/arm64-v8a/libcgw_webview.so: $(NATIVE)
	test -x "$(CC_ARM64)"
	mkdir -p build/lib/arm64-v8a
	"$(CC_ARM64)" $(FLAGS) $(NATIVE) -llog -o $@

build/fixture/lib/x86_64/libcgw_webview.so: $(NATIVE)
	test -x "$(CC_X86_64)"
	mkdir -p build/fixture/lib/x86_64
	"$(CC_X86_64)" $(FLAGS) -DCGW_WEBVIEW_SYNTHETIC $(NATIVE) -llog -o $@

build/arm64-unsigned.apk: build/lib/arm64-v8a/libcgw_webview.so AndroidManifest.xml assets/bridge.js
	test -f "$(DEX_DIR)/classes.dex"
	test -f "$(DEX_DIR)/classes2.dex"
	test -f "$(DEX_DIR)/classes3.dex"
	"$(BUILD_TOOLS)/aapt2" link -I "$(ANDROID_JAR)" --manifest AndroidManifest.xml --version-code 1 --version-name e1-source -o build/manifest.apk
	cp build/manifest.apk $@
	cp "$(DEX_DIR)"/classes*.dex build/
	cd build && zip -q arm64-unsigned.apk classes.dex classes2.dex classes3.dex lib/arm64-v8a/libcgw_webview.so
	zip -q -r build/arm64-unsigned.apk assets

build/fixture-unsigned.apk: build/fixture/lib/x86_64/libcgw_webview.so AndroidManifest.xml assets/bridge.js
	"$(BUILD_TOOLS)/aapt2" link -I "$(ANDROID_JAR)" --manifest AndroidManifest.xml --rename-manifest-package org.isomorphisms.chatgptexporter.e1fixture --version-code 1 --version-name e1-synthetic -o build/fixture-unsigned.apk
	cp "$(DEX_DIR)"/classes*.dex build/fixture/
	cd build/fixture && zip -q ../fixture-unsigned.apk classes.dex classes2.dex classes3.dex lib/x86_64/libcgw_webview.so
	zip -q -r build/fixture-unsigned.apk assets

fixture: build/fixture-unsigned.apk
	test -f "$(KEYSTORE)"
	test -n "$(CERT_SHA256)"
	keytool -exportcert -keystore "$(KEYSTORE)" -storepass android -alias androiddebugkey -file build/fixture-cert.der
	printf '%s  build/fixture-cert.der\n' '$(CERT_SHA256)' | sha256sum -c -
	"$(BUILD_TOOLS)/zipalign" -f 4 build/fixture-unsigned.apk build/fixture-aligned.apk
	"$(BUILD_TOOLS)/apksigner" sign --ks "$(KEYSTORE)" --ks-pass pass:android --key-pass pass:android --ks-key-alias androiddebugkey --out build/e1-synthetic.apk build/fixture-aligned.apk
	"$(BUILD_TOOLS)/apksigner" verify --verbose --print-certs build/e1-synthetic.apk

validate: build
	sha256sum build/arm64-unsigned.apk build/fixture-unsigned.apk build/lib/arm64-v8a/libcgw_webview.so build/fixture/lib/x86_64/libcgw_webview.so > build/build.sha256
	git rev-parse HEAD > build/source-commit.txt
	"$(BUILD_TOOLS)/aapt2" dump badging build/arm64-unsigned.apk > build/arm64-manifest.txt
	unzip -l build/arm64-unsigned.apk > build/arm64-contents.txt
	grep -q 'lib/arm64-v8a/libcgw_webview.so' build/arm64-contents.txt
	grep -q 'classes3.dex' build/arm64-contents.txt
