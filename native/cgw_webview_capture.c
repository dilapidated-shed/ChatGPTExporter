#include "cgw_webview_capture.h"
#include <string.h>

typedef struct {
    cgw_chunk_fn sink;
    void *context;
    size_t limit;
    size_t bytes;
    size_t chunks;
    int exceeded;
} bounded_sink;

static int accept_chunk(void *context, const unsigned char *bytes, size_t length)
{
    bounded_sink *b = context;
    if (length > b->limit - b->bytes) {
        b->exceeded = 1;
        return 0;
    }
    if (!b->sink(b->context, bytes, length)) return 0;
    b->bytes += length;
    ++b->chunks;
    return 1;
}

static cgw_fetch_result http_failure(unsigned status)
{
    if (status == 401) return CGW_FETCH_AUTH_REQUIRED;
    if (status == 403) return CGW_FETCH_WORKSPACE_FORBIDDEN;
    if (status == 429) return CGW_FETCH_RATE_LIMITED;
    return CGW_FETCH_HTTP_FAILURE;
}

cgw_fetch_result cgw_webview_capture_fetch(
    void *transport_context, const cgw_input *input, cgw_fetch_options options,
    cgw_chunk_fn sink, void *sink_context,
    cgw_fetch_observation *observation)
{
    if (observation) memset(observation, 0, sizeof(*observation));
    cgw_webview_capture_transport *t = transport_context;
    if (!observation || !t || !t->bridge || !t->exchange || !input || !sink
        || options.timeout_ms > 120000 || !options.response_cap_bytes)
        return CGW_FETCH_INVALID_INPUT;
    cgw_http_request plan;
    if (cgw_resolve(input, &plan) != CGW_OK)
        return CGW_FETCH_INVALID_INPUT;
    bounded_sink buffer = { sink, sink_context, options.response_cap_bytes, 0, 0, 0 };
    if (buffer.limit > plan.response_limit_bytes) buffer.limit = plan.response_limit_bytes;
    cgw_webview_command command;
    cgw_webview_result state = cgw_webview_bridge_begin(
        t->bridge, input, accept_chunk, &buffer, &command);
    if (state != CGW_WEBVIEW_OK) {
        return state == CGW_WEBVIEW_NOT_READY ? CGW_FETCH_AUTH_UNAVAILABLE
             : CGW_FETCH_INVALID_INPUT;
    }

    /* The platform exchange serializes all bridge operations on the UI
     * thread. No worker may directly call WebView or WebMessagePort methods. */
    state = t->exchange(t->exchange_context, &command, t->bridge,
                        options.timeout_ms ? options.timeout_ms : 30000);
    cgw_webview_observation received;
    cgw_webview_bridge_observation(t->bridge, &received);
    observation->http_status = received.http_status;
    observation->received_bytes = buffer.bytes;
    observation->received_chunks = buffer.chunks;
    observation->retryable = received.retryable;
    if (received.navigation_id != command.navigation_id) {
        (void)cgw_webview_bridge_cancel_request(t->bridge);
        return CGW_FETCH_NETWORK_FAILURE;
    }
    if (buffer.exceeded || state == CGW_WEBVIEW_RESPONSE_TOO_LARGE) {
        (void)cgw_webview_bridge_cancel_request(t->bridge);
        return CGW_FETCH_RESPONSE_TOO_LARGE;
    }
    if (state == CGW_WEBVIEW_HTTP_FAILURE)
        return http_failure(received.http_status);
    if (state == CGW_WEBVIEW_SINK_REJECTED) return CGW_FETCH_SINK_REJECTED;
    if (state != CGW_WEBVIEW_OK || !received.complete
        || received.http_status < 200 || received.http_status >= 300) {
        (void)cgw_webview_bridge_cancel_request(t->bridge);
        return CGW_FETCH_NETWORK_FAILURE;
    }
    return CGW_FETCH_OK;
}
