/* Optional native HTTPS effect adapter for the read-only CGW request planner.
 * Do not include this in the browser extension or use it to collect credentials.
 * All network operations use the planner's fixed https://chatgpt.com origin.
 */
#ifndef CGW_HTTPS_H
#define CGW_HTTPS_H

#include "cgw_web.h"
#include <stddef.h>

/* The caller supplies a short-lived token only when a validated operation
 * actually requires it. No token or cookie is acquired, logged, or retained
 * by this library. Callback returns 1 for a successful, NUL-terminated token
 * and its exact length; 0 rejects the request. */
typedef int (*cgw_token_fn)(void *context, char *buffer,
                            size_t capacity, size_t *length);

/* The sink writes into a REVERSIBLE staging location, never a completed
 * archive. It receives raw chunks before the final HTTP/result check.
 * Return 1 to accept, 0 to abort; discard staging on every non-OK result. */
typedef int (*cgw_chunk_fn)(void *context, const unsigned char *bytes,
                            size_t length);

typedef struct cgw_fetcher cgw_fetcher;
typedef enum {
    CGW_FETCH_OK = 0,
    CGW_FETCH_INVALID_INPUT,
    CGW_FETCH_AUTH_UNAVAILABLE,
    CGW_FETCH_TOKEN_INVALID,
    CGW_FETCH_NETWORK_FAILURE,
    CGW_FETCH_TLS_FAILURE,
    CGW_FETCH_HTTP_FAILURE,
    CGW_FETCH_AUTH_REQUIRED,
    CGW_FETCH_WORKSPACE_FORBIDDEN,
    CGW_FETCH_RATE_LIMITED,
    CGW_FETCH_RESPONSE_TOO_LARGE,
    CGW_FETCH_SINK_REJECTED,
    CGW_FETCH_SECRET_ECHO
} cgw_fetch_result;

typedef struct {
    /* Zero uses the endpoint planner's own maximum. A nonzero value may
     * only lower that maximum. */
    size_t response_cap_bytes;
    /* Zero uses a 30000 ms deadline; otherwise 1..120000. */
    unsigned timeout_ms;
} cgw_fetch_options;

typedef struct {
    unsigned http_status;       /* 0 if no HTTP response */
    size_t received_bytes;      /* includes rejected/HTTP-error response bytes */
    int retryable;              /* a hint, NOT an implicit retry */
} cgw_fetch_observation;

/* Workspace is optional for the personal account. A provided ID must be an
 * ASCII A-Z/a-z/0-9/_/- identifier (1..256 bytes). Only one workspace
 * belongs to a fetcher; never silently reuse a different account identity.
 * Returns NULL on invalid arguments or allocation failure. */
cgw_fetcher *cgw_fetcher_open(cgw_token_fn token, void *token_context,
                             const char *workspace_id);
void cgw_fetcher_close(cgw_fetcher *client);

/* Serial, synchronous, read-only. Never follows redirects or retries.
 * Token callback and sink are called only on the requesting thread.
 * The caller MUST roll back its staging sink on any non-OK result. */
cgw_fetch_result cgw_fetcher_execute(cgw_fetcher *client,
                                     const cgw_input *input,
                                     cgw_fetch_options options,
                                     cgw_chunk_fn sink,
                                     void *sink_context,
                                     cgw_fetch_observation *observation);

const char *cgw_fetch_result_name(cgw_fetch_result result);

#endif
