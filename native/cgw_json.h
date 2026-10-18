#ifndef CGW_JSON_H
#define CGW_JSON_H
#include <stddef.h>
#include <stdint.h>

/* Bounded syntax tree, never a rewritten representation of provider data. */
enum { CJ_MAX_TOKENS = 32768, CJ_MAX_DEPTH = 64 };
typedef enum { CJ_OBJECT, CJ_ARRAY, CJ_STRING, CJ_NUMBER, CJ_TRUE, CJ_FALSE, CJ_NULL } cj_kind;
typedef struct { cj_kind kind; size_t begin, end, next; } cj_token;
typedef struct {
    const unsigned char *bytes;
    size_t length, position, count, work, ceiling;
    cj_token *tokens;
} cj_document;
int cj_parse(cj_document *document, const unsigned char *bytes, size_t length, size_t work_ceiling);
void cj_free(cj_document *document);
size_t cj_member(cj_document *document, size_t object, const char *name);
int cj_text(const cj_document *document, size_t token, char *output, size_t capacity);
int cj_uint(const cj_document *document, size_t token, uint64_t *output);
#define CJ_MISSING ((size_t)-1)
#endif
