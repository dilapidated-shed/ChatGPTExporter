#ifndef CGW_CAPTURE_H
#define CGW_CAPTURE_H
#include "cgw_https.h"
#include <stdint.h>

typedef enum { CGW_CAPTURE_COMPLETE, CGW_CAPTURE_INCONCLUSIVE, CGW_CAPTURE_REFUSED } cgw_capture_result;
typedef struct {
    unsigned page_size, max_conversations, max_requests, max_graph_nodes;
    size_t max_response_bytes, max_json_work;
    uint64_t max_received_bytes;
} cgw_capture_bounds;
typedef struct {
    cgw_capture_result result;
    unsigned expected, completed, reused, requests;
    uint64_t received_bytes;
    uint64_t received_chunks;
    char workspace_fingerprint[65], inventory_sha256[65], report_sha256[65];
    char reason[80];
    int full_account_export_complete; /* Always false: this is one bounded scope. */
} cgw_capture_report;
/* An alternative fixed-origin, read-only transport. The callback receives a
 * validated typed operation; it must enforce the method/origin, bound raw
 * response bytes and return no credentials. It is synchronous on its caller's
 * thread, allowing a platform adapter to dispatch WebView I/O asynchronously
 * while a capture worker waits. No retries are implicit. */
typedef cgw_fetch_result (*cgw_capture_transport_fn)(
    void *context, const cgw_input *input, cgw_fetch_options options,
    cgw_chunk_fn sink, void *sink_context,
    cgw_fetch_observation *observation);

cgw_capture_bounds cgw_capture_default_bounds(void);
/* Read-only reconstruction after reopening private disk evidence. Replays the
 * latest run's two raw inventory passes and rehashes every expected graph and
 * marker; ignores exporter validation reports and RESULT completion flags. */
cgw_capture_result cgw_capture_revalidate(const char *private_root,
    const char *selected_workspace, cgw_capture_bounds bounds,
    cgw_capture_report *report);
cgw_capture_result cgw_capture_with_transport(
    const char *private_root, const char *selected_workspace,
    cgw_capture_transport_fn transport, void *transport_context,
    cgw_capture_bounds bounds, cgw_capture_report *report);
/* No executable live-account entrypoint is provided. Caller owns authorization
 * and explicitly selects one workspace. Fixture routing is compile-time only. */
cgw_capture_result cgw_capture(const char *private_root, const char *selected_workspace,
                              cgw_token_fn authorize, void *authorization_context,
                              cgw_capture_bounds bounds, cgw_capture_report *report);
#endif
