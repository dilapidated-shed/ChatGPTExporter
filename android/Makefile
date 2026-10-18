# E1 build evidence only. Production signing and delivery belong to E4.
# All toolchain locations are explicit inputs from the verified build host.
CC_ARM64 ?=
CC_X86_64 ?=
BUILD_TOOLS ?=
ANDROID_JAR ?=
DEX_DIR ?=
DEX_SOURCE ?=
KEYSTORE ?=
CERT_SHA256 ?=
NATIVE = ../native/cgw_web.c ../native/cgw_webview_bridge.c cgw_webview_android.c
FLAGS = -std=c11 -O2 -Wall -Wextra -Werror -fPIC -shared -Wl,-z,max-page-size=16384 -I../native

.PHONY: build native fixture validate verify-grease verify-dex
native: build/lib/arm64-v8a/libcgw_webview.so build/fixture/lib/x86_64/libcgw_webview.so
verify-grease:
	printf '%s  %s\n' '7e31cd05b7a9d8fb2a4a9e003a7f3fcb0159138506d17f0fb28da8cbe22aa85c' '$(GREASE)' | sha256sum -c -
	chmod +x "$(GREASE)"
	ASAN_OPTIONS=detect_leaks=0 "$(GREASE)" -n qualify.grease > /dev/null
build: verify-dex build/arm64-unsigned.apk build/fixture-unsigned.apk

verify-dex:
	test -n "$(DEX_SOURCE)"
	grep -qx "$(DEX_SOURCE)" "$(DEX_DIR)/source-commit.txt"

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
	keytool -exportcert -keystore "$(KEYSTORE)" -storetype PKCS12 -storepass wegert-debug -alias wegert-debug -file build/fixture-cert.der
	printf '%s  build/fixture-cert.der\n' '$(CERT_SHA256)' | sha256sum -c -
	"$(BUILD_TOOLS)/zipalign" -f 4 build/fixture-unsigned.apk build/fixture-aligned.apk
	"$(BUILD_TOOLS)/apksigner" sign --ks "$(KEYSTORE)" --ks-type PKCS12 --ks-pass pass:wegert-debug --key-pass pass:wegert-debug --ks-key-alias wegert-debug --out build/e1-synthetic.apk build/fixture-aligned.apk
	"$(BUILD_TOOLS)/apksigner" verify --verbose --print-certs build/e1-synthetic.apk

validate: build
	sha256sum build/arm64-unsigned.apk build/fixture-unsigned.apk build/lib/arm64-v8a/libcgw_webview.so build/fixture/lib/x86_64/libcgw_webview.so > build/build.sha256
	sha256sum AndroidManifest.xml assets/bridge.js cgw_webview_android.c "$(DEX_DIR)/classes.dex" "$(DEX_DIR)/classes2.dex" "$(DEX_DIR)/classes3.dex" >> build/build.sha256
	"$(CC_ARM64)" --version > build/compiler.txt
	git rev-parse HEAD > build/source-commit.txt
	"$(BUILD_TOOLS)/aapt2" dump badging build/arm64-unsigned.apk > build/arm64-manifest.txt
	unzip -l build/arm64-unsigned.apk > build/arm64-contents.txt
	grep -q 'lib/arm64-v8a/libcgw_webview.so' build/arm64-contents.txt
	grep -q 'classes3.dex' build/arm64-contents.txt

# Explicitly requested C67 development trial; signs the qualified ARM64 input.
.PHONY: c67-trial
c67-trial:
	printf '%s  build/arm64-unsigned.apk\n' '5df55daaec4ab50f3916d272f1239c5c2150d4de7862677840b76ade5d74764c' | sha256sum -c -
	grep -qx '7ff59674079d57bea4e366f36ae455cd9d5fc3dc' build/source-commit.txt
	keytool -exportcert -keystore "$(KEYSTORE)" -storetype PKCS12 -storepass wegert-debug -alias wegert-debug -file build/c67-cert.der
	printf '%s  build/c67-cert.der\n' 'de9b1d47c5a65e6d46a204b79dd9ee566b9d3c9832ba81ebc4213d3392e92ff9' | sha256sum -c -
	"$(BUILD_TOOLS)/zipalign" -P 16 -f 4 build/arm64-unsigned.apk build/c67-aligned.apk
	"$(BUILD_TOOLS)/apksigner" sign --ks "$(KEYSTORE)" --ks-type PKCS12 --ks-pass pass:wegert-debug --key-pass pass:wegert-debug --ks-key-alias wegert-debug --out build/chatgpt-exporter-c67-e1.apk build/c67-aligned.apk
	"$(BUILD_TOOLS)/apksigner" verify --verbose --print-certs build/chatgpt-exporter-c67-e1.apk > build/c67-signature.txt
	grep -q 'certificate SHA-256 digest: de9b1d47c5a65e6d46a204b79dd9ee566b9d3c9832ba81ebc4213d3392e92ff9' build/c67-signature.txt
	"$(BUILD_TOOLS)/zipalign" -c -P 16 4 build/chatgpt-exporter-c67-e1.apk
	"$(BUILD_TOOLS)/aapt2" dump badging build/chatgpt-exporter-c67-e1.apk > build/c67-manifest.txt
	grep -q "native-code: 'arm64-v8a'" build/c67-manifest.txt
	sha256sum build/chatgpt-exporter-c67-e1.apk > build/c67.sha256
