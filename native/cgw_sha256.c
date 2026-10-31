#include "cgw_sha256.h"
#include <limits.h>
#include <string.h>

static const uint32_t k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};
static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }
static uint32_t read_be(const unsigned char *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16)
        | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}
static void write_be(unsigned char *bytes, uint32_t word)
{
    bytes[0] = (unsigned char)(word >> 24);
    bytes[1] = (unsigned char)(word >> 16);
    bytes[2] = (unsigned char)(word >> 8);
    bytes[3] = (unsigned char)word;
}
static void block(cgw_sha256 *ctx, const unsigned char *data)
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) w[i] = read_be(data + 4 * i);
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t a = w[i - 15], b = w[i - 2];
        uint32_t s0 = rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3);
        uint32_t s1 = rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t big0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        uint32_t big1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        uint32_t choose = (e & f) ^ (~e & g);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t1 = h + big1 + choose + k[i] + w[i];
        uint32_t t2 = big0 + majority;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
    memset(w, 0, sizeof(w));
}
void cgw_sha256_init(cgw_sha256 *ctx)
{
    if (!ctx) return;
    static const uint32_t initial[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->state, initial, sizeof(initial));
}
int cgw_sha256_update(cgw_sha256 *ctx, const void *bytes, size_t count)
{
    if (!ctx || (!bytes && count)) return 0;
    if (count > (uint64_t)(UINT64_MAX / 8u - ctx->bytes)) return 0;
    const unsigned char *p = bytes;
    ctx->bytes += (uint64_t)count;
    while (count) {
        if (!ctx->buffered && count >= 64) {
            block(ctx, p);
            p += 64; count -= 64; continue;
        }
        size_t amount = 64 - ctx->buffered;
        if (amount > count) amount = count;
        memcpy(ctx->buffer + ctx->buffered, p, amount);
        ctx->buffered += amount;
        p += amount; count -= amount;
        if (ctx->buffered == 64) { block(ctx, ctx->buffer); ctx->buffered = 0; }
    }
    return 1;
}
int cgw_sha256_final(cgw_sha256 *ctx, unsigned char digest[32])
{
    if (!ctx || !digest) return 0;
    uint64_t bits = ctx->bytes * UINT64_C(8);
    ctx->buffer[ctx->buffered++] = 0x80;
    if (ctx->buffered > 56) {
        memset(ctx->buffer + ctx->buffered, 0, 64 - ctx->buffered);
        block(ctx, ctx->buffer); ctx->buffered = 0;
    }
    memset(ctx->buffer + ctx->buffered, 0, 56 - ctx->buffered);
    for (unsigned i = 0; i < 8; ++i)
        ctx->buffer[56 + i] = (unsigned char)(bits >> (56 - 8 * i));
    block(ctx, ctx->buffer);
    for (unsigned i = 0; i < 8; ++i) write_be(digest + 4 * i, ctx->state[i]);
    memset(ctx, 0, sizeof(*ctx));
    return 1;
}
int cgw_sha256_hash(const void *bytes, size_t count, unsigned char digest[32])
{
    cgw_sha256 ctx;
    cgw_sha256_init(&ctx);
    return cgw_sha256_update(&ctx, bytes, count) && cgw_sha256_final(&ctx, digest);
}
void cgw_sha256_hex(const unsigned char digest[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';
}
