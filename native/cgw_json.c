#include "cgw_json.h"
#include <stdlib.h>
#include <string.h>

static int charge(cj_document *d) { return ++d->work <= d->ceiling; }
static void space(cj_document *d)
{
    while (d->position < d->length && (d->bytes[d->position] == ' ' || d->bytes[d->position] == '\n'
           || d->bytes[d->position] == '\r' || d->bytes[d->position] == '\t')) ++d->position;
}
static int hex(unsigned char b)
{
    if (b >= '0' && b <= '9') return b - '0';
    if (b >= 'a' && b <= 'f') return b - 'a' + 10;
    if (b >= 'A' && b <= 'F') return b - 'A' + 10;
    return -1;
}
static int scalar(const unsigned char *s, size_t end, size_t *position, unsigned *value)
{
    size_t p = *position;
    if (p >= end) return 0;
    unsigned v = s[p++];
    if (v == '\\') {
        if (p >= end) return 0;
        unsigned e = s[p++];
        if (e == 'u') {
            v = 0;
            for (int i = 0; i < 4; ++i) { if (p >= end || hex(s[p]) < 0) return 0; v = (v << 4) | (unsigned)hex(s[p++]); }
            if (v >= 0xd800 && v <= 0xdbff) {
                if (p + 6 > end || s[p++] != '\\' || s[p++] != 'u') return 0;
                unsigned low = 0;
                for (int i = 0; i < 4; ++i) { if (hex(s[p]) < 0) return 0; low = (low << 4) | (unsigned)hex(s[p++]); }
                if (low < 0xdc00 || low > 0xdfff) return 0;
                v = 0x10000 + ((v - 0xd800) << 10) + low - 0xdc00;
            } else if (v >= 0xdc00 && v <= 0xdfff) return 0;
        } else {
            switch (e) {
            case '"': v = '"'; break; case '\\': v = '\\'; break; case '/': v = '/'; break;
            case 'b': v = 8; break; case 'f': v = 12; break; case 'n': v = 10; break;
            case 'r': v = 13; break; case 't': v = 9; break; default: return 0;
            }
        }
    } else if (v >= 0x80) {
        unsigned minimum; int extra;
        if (v >= 0xc2 && v <= 0xdf) { extra = 1; minimum = 0x80; v &= 31; }
        else if (v >= 0xe0 && v <= 0xef) { extra = 2; minimum = 0x800; v &= 15; }
        else if (v >= 0xf0 && v <= 0xf4) { extra = 3; minimum = 0x10000; v &= 7; }
        else return 0;
        while (extra--) { if (p >= end || (s[p] & 0xc0) != 0x80) return 0; v = (v << 6) | (s[p++] & 63); }
        if (v < minimum || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
    } else if (v < 32 || v == '"') return 0;
    *position = p; *value = v; return 1;
}
static int same_key(cj_document *d, size_t a, size_t b)
{
    size_t p = d->tokens[a].begin, q = d->tokens[b].begin;
    while (p < d->tokens[a].end && q < d->tokens[b].end) {
        unsigned x, y;
        if (!charge(d) || !scalar(d->bytes, d->tokens[a].end, &p, &x) || !scalar(d->bytes, d->tokens[b].end, &q, &y)) return -1;
        if (x != y) return 0;
    }
    return p == d->tokens[a].end && q == d->tokens[b].end;
}
static int value(cj_document *d, unsigned depth)
{
    space(d);
    if (!charge(d) || depth > CJ_MAX_DEPTH || d->position >= d->length || d->count >= CJ_MAX_TOKENS) return 0;
    size_t index = d->count++;
    cj_token *t = &d->tokens[index]; t->begin = d->position;
    unsigned char first = d->bytes[d->position++];
    if (first == '"') {
        t->kind = CJ_STRING; t->begin = d->position;
        while (d->position < d->length && d->bytes[d->position] != '"') {
            unsigned decoded;
            if (!charge(d) || !scalar(d->bytes, d->length, &d->position, &decoded)) return 0;
        }
        if (d->position >= d->length) return 0;
        t->end = d->position++;
    } else if (first == '{' || first == '[') {
        int object = first == '{'; unsigned char close = object ? '}' : ']'; t->kind = object ? CJ_OBJECT : CJ_ARRAY;
        space(d);
        if (d->position < d->length && d->bytes[d->position] == close) ++d->position;
        else for (;;) {
            size_t key = d->count;
            if (!value(d, depth + 1)) return 0;
            if (object) {
                if (d->tokens[key].kind != CJ_STRING) return 0;
                for (size_t old = index + 1; old < key; old = d->tokens[d->tokens[old].next].next) {
                    int equal = same_key(d, old, key); if (equal != 0) return 0;
                }
                space(d);
                if (d->position >= d->length || d->bytes[d->position++] != ':' || !value(d, depth + 1)) return 0;
            }
            space(d);
            if (d->position >= d->length) return 0;
            unsigned char separator = d->bytes[d->position++];
            if (separator == close) break;
            if (separator != ',') return 0;
        }
        t->end = d->position;
    } else if (first == 't' || first == 'f' || first == 'n') {
        const char *literal = first == 't' ? "true" : first == 'f' ? "false" : "null";
        size_t length = strlen(literal);
        if (t->begin + length > d->length || memcmp(d->bytes + t->begin, literal, length)) return 0;
        d->position = t->begin + length; t->end = d->position;
        t->kind = first == 't' ? CJ_TRUE : first == 'f' ? CJ_FALSE : CJ_NULL;
    } else {
        t->kind = CJ_NUMBER; d->position = t->begin;
        if (d->bytes[d->position] == '-') ++d->position;
        if (d->position >= d->length) return 0;
        if (d->bytes[d->position] == '0') ++d->position;
        else {
            if (d->bytes[d->position] < '1' || d->bytes[d->position] > '9') return 0;
            do { ++d->position; } while (d->position < d->length && d->bytes[d->position] >= '0' && d->bytes[d->position] <= '9');
        }
        if (d->position < d->length && d->bytes[d->position] == '.') {
            size_t start = ++d->position;
            while (d->position < d->length && d->bytes[d->position] >= '0' && d->bytes[d->position] <= '9') ++d->position;
            if (d->position == start) return 0;
        }
        if (d->position < d->length && (d->bytes[d->position] == 'e' || d->bytes[d->position] == 'E')) {
            ++d->position;
            if (d->position < d->length && (d->bytes[d->position] == '+' || d->bytes[d->position] == '-')) ++d->position;
            size_t start = d->position;
            while (d->position < d->length && d->bytes[d->position] >= '0' && d->bytes[d->position] <= '9') ++d->position;
            if (d->position == start) return 0;
        }
        t->end = d->position;
    }
    t->next = d->count; return 1;
}
int cj_parse(cj_document *d, const unsigned char *bytes, size_t length, size_t ceiling)
{
    memset(d, 0, sizeof(*d)); d->bytes = bytes; d->length = length; d->ceiling = ceiling;
    d->tokens = calloc(CJ_MAX_TOKENS, sizeof(*d->tokens));
    if (!d->tokens || !value(d, 0)) { cj_free(d); return 0; }
    space(d);
    if (d->position != length) { cj_free(d); return 0; }
    return 1;
}
void cj_free(cj_document *d) { free(d->tokens); d->tokens = NULL; }
int cj_text(const cj_document *d, size_t index, char *out, size_t capacity)
{
    if (index == CJ_MISSING || index >= d->count || d->tokens[index].kind != CJ_STRING || capacity == 0) return 0;
    size_t position = d->tokens[index].begin, used = 0;
    while (position < d->tokens[index].end) {
        unsigned v; if (!scalar(d->bytes, d->tokens[index].end, &position, &v) || v == 0) return 0;
        unsigned char encoded[4]; size_t n;
        if (v < 128) { encoded[0] = (unsigned char)v; n = 1; }
        else if (v < 2048) { encoded[0] = (unsigned char)(0xc0 | (v >> 6)); encoded[1] = (unsigned char)(0x80 | (v & 63)); n = 2; }
        else if (v < 65536) { encoded[0] = (unsigned char)(0xe0 | (v >> 12)); encoded[1] = (unsigned char)(0x80 | ((v >> 6) & 63)); encoded[2] = (unsigned char)(0x80 | (v & 63)); n = 3; }
        else { encoded[0] = (unsigned char)(0xf0 | (v >> 18)); encoded[1] = (unsigned char)(0x80 | ((v >> 12) & 63)); encoded[2] = (unsigned char)(0x80 | ((v >> 6) & 63)); encoded[3] = (unsigned char)(0x80 | (v & 63)); n = 4; }
        if (n >= capacity - used) return 0;
        memcpy(out + used, encoded, n); used += n;
    }
    out[used] = 0; return 1;
}
size_t cj_member(cj_document *d, size_t object, const char *name)
{
    if (object >= d->count || d->tokens[object].kind != CJ_OBJECT) return CJ_MISSING;
    for (size_t key = object + 1; key < d->tokens[object].next; key = d->tokens[d->tokens[key].next].next) {
        char decoded[257];
        if (!charge(d)) return CJ_MISSING;
        if (cj_text(d, key, decoded, sizeof(decoded)) && !strcmp(decoded, name)) return d->tokens[key].next;
    }
    return CJ_MISSING;
}
int cj_uint(const cj_document *d, size_t index, uint64_t *out)
{
    if (index == CJ_MISSING || index >= d->count || d->tokens[index].kind != CJ_NUMBER) return 0;
    uint64_t n = 0;
    for (size_t p = d->tokens[index].begin; p < d->tokens[index].end; ++p) {
        unsigned char b = d->bytes[p]; if (b < '0' || b > '9' || n > (UINT64_MAX - (b - '0')) / 10) return 0;
        n = n * 10 + b - '0';
    }
    *out = n; return 1;
}
