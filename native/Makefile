CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic

.PHONY: check check-webview clean
build/test_cgw_web: cgw_web.c cgw_web.h test_cgw_web.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_web.c test_cgw_web.c

check: build/test_cgw_web
	./build/test_cgw_web

build/test_cgw_webview_bridge: cgw_web.c cgw_web.h cgw_webview_bridge.c cgw_webview_bridge.h test_cgw_webview_bridge.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_web.c cgw_webview_bridge.c test_cgw_webview_bridge.c

check-webview: build/test_cgw_webview_bridge
	./build/test_cgw_webview_bridge

clean:
	rm -rf build

CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS := $(shell pkg-config --libs libcurl 2>/dev/null)

.PHONY: check-https
build/test_cgw_https: cgw_web.c cgw_web.h cgw_https.c cgw_https.h cgw_object_store.c cgw_object_store.h cgw_sha256.c cgw_sha256.h test_cgw_https.c
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -o $@ \
	  cgw_web.c cgw_https.c cgw_object_store.c cgw_sha256.c test_cgw_https.c $(CURL_LIBS) -lcrypto

check-https: build/test_cgw_https
	bash fixtures/run_https.sh

.PHONY: check-store
build/test_cgw_object_store: cgw_object_store.c cgw_object_store.h cgw_sha256.c cgw_sha256.h test_cgw_object_store.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_object_store.c cgw_sha256.c test_cgw_object_store.c -lcrypto

check-store: build/test_cgw_object_store
	./build/test_cgw_object_store

# Host/loopback fixtures only. Production qualification must explicitly select
# the pinned ICK producer and shared ai-ci toolchain contract in hosted CI.
CAPTURE_SOURCES = cgw_web.c cgw_https.c cgw_object_store.c cgw_sha256.c cgw_json.c cgw_capture.c test_cgw_capture.c
CAPTURE_HEADERS = cgw_web.h cgw_https.h cgw_object_store.h cgw_sha256.h cgw_json.h cgw_capture.h
.PHONY: check-capture check-capture-mutants
build/test_cgw_capture: $(CAPTURE_SOURCES) $(CAPTURE_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -o $@ $(CAPTURE_SOURCES) $(CURL_LIBS) -lssl -lcrypto

check-capture: build/test_cgw_capture
	./build/test_cgw_capture

build/mutant-premature: $(CAPTURE_SOURCES) $(CAPTURE_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -DCGW_MUTANT_PREMATURE_COMPLETE -o $@ $(CAPTURE_SOURCES) $(CURL_LIBS) -lssl -lcrypto
build/mutant-omission: $(CAPTURE_SOURCES) $(CAPTURE_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -DCGW_MUTANT_SILENT_OMISSION -o $@ $(CAPTURE_SOURCES) $(CURL_LIBS) -lssl -lcrypto
build/mutant-marker: $(CAPTURE_SOURCES) $(CAPTURE_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -DCGW_MUTANT_INVALID_MARKER -o $@ $(CAPTURE_SOURCES) $(CURL_LIBS) -lssl -lcrypto
check-capture-mutants: build/mutant-premature build/mutant-omission build/mutant-marker
	./build/mutant-premature --mutant
	./build/mutant-omission --mutant
	./build/mutant-marker --mutant

.PHONY: production
production: build/production/cgw_web.o build/production/cgw_https.o build/production/cgw_object_store.o build/production/cgw_sha256.o build/production/cgw_json.o build/production/cgw_capture.o
build/production/%.o: %.c $(CAPTURE_HEADERS)
	mkdir -p build/production
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -c $< -o $@

# Credential-free WebView transport through the *same* durable coordinator.
# This test deliberately models the page-port exchange; no ChatGPT account.
WEBVIEW_CAPTURE_SOURCES = cgw_web.c cgw_https.c cgw_object_store.c cgw_sha256.c cgw_json.c cgw_capture.c cgw_webview_bridge.c cgw_webview_capture.c test_cgw_webview_capture.c
WEBVIEW_CAPTURE_HEADERS = $(CAPTURE_HEADERS) cgw_webview_bridge.h cgw_webview_capture.h
.PHONY: check-webview-capture
build/test_cgw_webview_capture: $(WEBVIEW_CAPTURE_SOURCES) $(WEBVIEW_CAPTURE_HEADERS)
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -o $@ $(WEBVIEW_CAPTURE_SOURCES) $(CURL_LIBS) -lssl -lcrypto

check-webview-capture: build/test_cgw_webview_capture
	./build/test_cgw_webview_capture

.PHONY: check-sha256
build/test_cgw_sha256: cgw_sha256.c cgw_sha256.h test_cgw_sha256.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_sha256.c test_cgw_sha256.c

check-sha256: build/test_cgw_sha256
	./build/test_cgw_sha256
