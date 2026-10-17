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
