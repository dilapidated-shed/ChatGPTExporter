#ifndef CGW_SHA256_H
#define CGW_SHA256_H
#include <stddef.h>
#include <stdint.h>

/* Portable, bounded SHA-256 for immutable archive objects. No platform
 * libcrypto symbols, heap allocations, device-ABI assumptions or credentials.
 * A context is single-use after finalization. */
typedef struct {
    uint32_t state[8];
    unsigned char buffer[64];
    uint64_t bytes;
    size_t buffered;
} cgw_sha256;

void cgw_sha256_init(cgw_sha256 *ctx);
/* Returns 0 for invalid input or an unrepresentable SHA-256 bit length. */
int cgw_sha256_update(cgw_sha256 *ctx, const void *bytes, size_t count);
int cgw_sha256_final(cgw_sha256 *ctx, unsigned char digest[32]);
int cgw_sha256_hash(const void *bytes, size_t count, unsigned char digest[32]);
void cgw_sha256_hex(const unsigned char digest[32], char hex[65]);
#endif
