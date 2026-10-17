CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror -pedantic

.PHONY: check clean
build/test_cgw_web: cgw_web.c cgw_web.h test_cgw_web.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_web.c test_cgw_web.c

check: build/test_cgw_web
	./build/test_cgw_web

clean:
	rm -rf build

CURL_CFLAGS := $(shell pkg-config --cflags libcurl)
CURL_LIBS := $(shell pkg-config --libs libcurl)

.PHONY: check-https
build/test_cgw_https: cgw_web.c cgw_web.h cgw_https.c cgw_https.h cgw_object_store.c cgw_object_store.h test_cgw_https.c
	mkdir -p build
	$(CC) $(CFLAGS) $(CURL_CFLAGS) -DCGW_TEST_LOCAL_TLS -o $@ \
	  cgw_web.c cgw_https.c cgw_object_store.c test_cgw_https.c $(CURL_LIBS) -lcrypto

check-https: build/test_cgw_https
	bash fixtures/run_https.sh

.PHONY: check-store
build/test_cgw_object_store: cgw_object_store.c cgw_object_store.h test_cgw_object_store.c
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ cgw_object_store.c test_cgw_object_store.c -lcrypto

check-store: build/test_cgw_object_store
	./build/test_cgw_object_store
