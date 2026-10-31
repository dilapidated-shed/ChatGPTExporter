#ifndef CGW_WEBVIEW_CAPTURE_H
#define CGW_WEBVIEW_CAPTURE_H

#include "cgw_capture.h"
#include "cgw_webview_bridge.h"

/* Platform adapter boundary. Exchange must deliver the prepared command to
 * the VERIFIED main-document port and process replies until terminal or
 * timeout. It is invoked on the capture worker, NEVER on the WebView/UI
 * thread. Its implementation must wake promptly on navigation/destruction.
 * It may call cgw_webview_bridge_receive only in a serialized context. */
typedef cgw_webview_result (*cgw_webview_capture_exchange_fn)(
    void *context, const cgw_webview_command *command,
    cgw_webview_bridge *bridge, unsigned timeout_ms);

typedef struct {
    cgw_webview_bridge *bridge;
    cgw_webview_capture_exchange_fn exchange;
    void *exchange_context;
} cgw_webview_capture_transport;

/* Use with cgw_capture_with_transport. There is deliberately no token
 * callback, cookie store, native HTTPS fallback or editable URL. */
cgw_fetch_result cgw_webview_capture_fetch(
    void *transport_context, const cgw_input *input, cgw_fetch_options options,
    cgw_chunk_fn sink, void *sink_context,
    cgw_fetch_observation *observation);

#endif
