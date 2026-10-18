/* Bounded page-world bridge for a future Android WebView adapter.
 *
 * The adapter may retain an interactive WebView session, but this C layer
 * never reads cookies, bearer tokens, browser profiles, or arbitrary URLs.
 * It serializes only cgw_resolve allowlist requests and accepts only the
 * exact reply grammar below from a same-origin main document.
 */
#ifndef CGW_WEBVIEW_BRIDGE_H
#define CGW_WEBVIEW_BRIDGE_H

#include "cgw_web.h"

#include <stddef.h>
#include <stdint.h>

enum {
    CGW_WEBVIEW_NONCE_HEX_LENGTH = 32,
    CGW_WEBVIEW_COMMAND_CAPACITY = 16384,
    CGW_WEBVIEW_MESSAGE_CAPACITY = 16384
};

typedef struct cgw_webview_bridge cgw_webview_bridge;

/* The platform supplies kernel-backed entropy.  Returning zero leaves no
 * bridge object.  rand(), time and request identifiers are not substitutes. */
typedef int (*cgw_webview_entropy_fn)(void *context, unsigned char *bytes,
                                      size_t length);

/* The sink receives raw response chunks only after all bridge checks for that
 * chunk pass.  Returning zero aborts the active request without a receipt. */
typedef int (*cgw_webview_chunk_fn)(void *context,
                                    const unsigned char *bytes, size_t length);

typedef struct {
    cgw_webview_entropy_fn entropy;
    void *entropy_context;
    /* Zero preserves cgw_resolve's per-operation bound.  A nonzero value may
     * only lower it; this is useful for a caller's memory/disk budget. */
    size_t response_cap_bytes;
} cgw_webview_bridge_config;

typedef enum {
    CGW_WEBVIEW_OK = 0,
    CGW_WEBVIEW_INVALID_ARGUMENT,
    CGW_WEBVIEW_ORIGIN_REJECTED,
    CGW_WEBVIEW_FRAME_REJECTED,
    CGW_WEBVIEW_NOT_READY,
    CGW_WEBVIEW_BUSY,
    CGW_WEBVIEW_REQUEST_REJECTED,
    CGW_WEBVIEW_ENTROPY_FAILURE,
    CGW_WEBVIEW_NO_ACTIVE_REQUEST,
    CGW_WEBVIEW_INVALID_WIRE,
    CGW_WEBVIEW_REPLAY_REJECTED,
    CGW_WEBVIEW_RESPONSE_TOO_LARGE,
    CGW_WEBVIEW_HTTP_FAILURE,
    CGW_WEBVIEW_SINK_REJECTED,
    CGW_WEBVIEW_NAVIGATION_CANCELLED
} cgw_webview_result;

typedef struct {
    uint64_t navigation_id;
    uint64_t request_id;
    unsigned http_status;
    size_t received_bytes;
    int complete;
    int retryable;
} cgw_webview_observation;

typedef struct {
    uint64_t navigation_id;
    uint64_t request_id;
    char nonce[CGW_WEBVIEW_NONCE_HEX_LENGTH + 1];
    /* ASCII protocol, emitted only after cgw_resolve validates the operation:
     * CGW1;request;<nonce>;<id>;<GET|POST>;<relative-path>;<base64url-body>
     * The WebView adapter must deliver it to the already established page
     * port.  It must never reinterpret it as a caller-chosen URL or method. */
    char wire[CGW_WEBVIEW_COMMAND_CAPACITY];
} cgw_webview_command;

/* Open a serial bridge.  It starts with no verified page. */
cgw_webview_bridge *cgw_webview_bridge_open(
    const cgw_webview_bridge_config *config);
void cgw_webview_bridge_close(cgw_webview_bridge *bridge);

/* The Android adapter calls this before every top-level navigation.  Any
 * request still in flight becomes cancelled and its caller-owned stage must be
 * rolled back. */
cgw_webview_result cgw_webview_bridge_navigation_started(
    cgw_webview_bridge *bridge);

/* Mark one committed document.  The origin must be exactly
 * https://chatgpt.com and the platform must affirm a main frame. */
cgw_webview_result cgw_webview_bridge_document_committed(
    cgw_webview_bridge *bridge, const char *origin, int main_frame);

/* Marks the page-port bootstrap ready for the current committed document. */
cgw_webview_result cgw_webview_bridge_port_ready(cgw_webview_bridge *bridge);

/* Resolve one typed, read-only operation and prepare exactly one wire command.
 * The bridge owns request-id, nonce, response limit and chunk sequence. */
cgw_webview_result cgw_webview_bridge_begin(
    cgw_webview_bridge *bridge, const cgw_input *input,
    cgw_webview_chunk_fn sink, void *sink_context,
    cgw_webview_command *command);

/* Accept only this page-port reply grammar:
 * CGW1;reply;<nonce>;<id>;<HTTP-status>;<sequence>;<final-0-or-1>;<base64url>
 * A non-2xx status must be sequence 0, final 1 and empty payload.  The bridge
 * rejects wrong nonces, replayed identifiers, out-of-order chunks, malformed
 * base64url, oversize responses and messages after navigation. */
cgw_webview_result cgw_webview_bridge_receive(
    cgw_webview_bridge *bridge, const char *wire, size_t length);

void cgw_webview_bridge_observation(const cgw_webview_bridge *bridge,
                                    cgw_webview_observation *output);
const char *cgw_webview_result_name(cgw_webview_result result);

#endif
