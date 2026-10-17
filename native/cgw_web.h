/* Credential-free, read-only ChatGPT web-history request planning.
 * Private web endpoints are unstable. This is not an OpenAI public API client.
 * No authorization, cookies, arbitrary URLs, transport, or file writes here.
 */
#ifndef CGW_WEB_H
#define CGW_WEB_H

#include <stddef.h>
#include <stdint.h>

enum { CGW_PATH_CAPACITY = 8192, CGW_BODY_CAPACITY = 4096 };

typedef enum {
    CGW_SESSION_PROBE = 1,
    CGW_ACCOUNTS_LIST,
    CGW_CONVERSATION_PAGE,
    CGW_PROJECT_PAGE,
    CGW_PROJECT_CONVERSATION_PAGE,
    CGW_SHARED_PAGE,
    CGW_SHARED_DETAIL,
    CGW_CONVERSATION_BATCH,
    CGW_CONVERSATION_CURRENT,
    CGW_CONVERSATION_MESSAGES,
    CGW_CONVERSATION_DETAIL,
    CGW_ACCOUNT_ARTIFACT,
    CGW_ASSET_OPEN
} cgw_operation;

typedef enum {
    CGW_MEMORIES = 1,
    CGW_CUSTOM_INSTRUCTIONS,
    CGW_SETTINGS,
    CGW_BETA_FEATURES
} cgw_artifact_kind;

typedef enum {
    CGW_ASSET_CONVERSATION = 1,
    CGW_ASSET_PROJECT
} cgw_asset_scope;

typedef enum { CGW_GET = 1, CGW_POST } cgw_method;
typedef enum {
    CGW_OK = 0,
    CGW_INVALID_OPERATION,
    CGW_INVALID_PARAMETERS,
    CGW_CAPACITY_EXCEEDED
} cgw_status;

/* The union makes operation-specific arguments explicit.
 * All pointers are borrowed for the duration of cgw_resolve only.
 */
typedef struct {
    cgw_operation operation;
    union {
        struct { uint64_t offset; unsigned limit; int archived; } conversation_page;
        struct { uint64_t offset; unsigned limit; } shared_page;
        struct { const char *cursor; } project_page; /* NULL means first page */
        struct { const char *project_id; const char *cursor; } project_conversation_page;
        struct { const char *id; } detail;
        struct { const char *conversation_id; unsigned turns; } current;
        struct { const char *conversation_id; const char *before; unsigned turns; } messages;
        struct { const char *const *conversation_ids; size_t count; } batch;
        struct { cgw_artifact_kind kind; } artifact;
        struct { const char *file_id; const char *parent_id; cgw_asset_scope scope; } asset;
    } arguments;
} cgw_input;

typedef struct {
    cgw_method method;
    char path[CGW_PATH_CAPACITY]; /* relative to https://chatgpt.com only */
    char body[CGW_BODY_CAPACITY]; /* JSON for one bounded read-only POST */
    size_t response_limit_bytes;
    int requires_authentication;
} cgw_http_request;

/* Never performs I/O. Returns a cleared output on every rejection.
 * Caller MUST enforce HTTPS origin, auth isolation, method, byte limit,
 * response parsing and filesystem policy independently.
 */
cgw_status cgw_resolve(const cgw_input *input, cgw_http_request *output);
const char *cgw_status_name(cgw_status status);

#endif
