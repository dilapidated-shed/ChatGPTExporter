#include "cgw_sha256.h"
#include <stdio.h>
#include <string.h>

static int check(const void *bytes, size_t length, const char *expected, size_t step)
{
    cgw_sha256 ctx; cgw_sha256_init(&ctx);
    const unsigned char *p = bytes;
    for (size_t i = 0; i < length;) {
        size_t n = length - i; if (n > step) n = step;
        if (!cgw_sha256_update(&ctx, p + i, n)) return 0;
        i += n;
    }
    unsigned char digest[32]; char actual[65];
    if (!cgw_sha256_final(&ctx, digest)) return 0;
    cgw_sha256_hex(digest, actual);
    if (strcmp(actual, expected)) {
        fprintf(stderr, "SHA-256 mismatch for length %zu: %s\n", length, actual);
        return 0;
    }
    return 1;
}
int main(void)
{
    const char *empty = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char *abc = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char *million = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
    char bulk[1000000]; memset(bulk, 'a', sizeof(bulk));
    int ok = check("", 0, empty, 1) && check("abc", 3, abc, 1)
        && check("abc", 3, abc, 3)
        && check(bulk, sizeof(bulk), million, 64)
        && check(bulk, sizeof(bulk), million, 8191)
        && check(bulk, sizeof(bulk), million, sizeof(bulk));
    puts(ok ? "portable_sha256_known_vectors=PASS" : "portable_sha256_known_vectors=FAIL");
    return !ok;
}
