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
cgw_capture_bounds cgw_capture_default_bounds(void);
/* No executable live-account entrypoint is provided. Caller owns authorization
 * and explicitly selects one workspace. Fixture routing is compile-time only. */
cgw_capture_result cgw_capture(const char *private_root, const char *selected_workspace,
                              cgw_token_fn authorize, void *authorization_context,
                              cgw_capture_bounds bounds, cgw_capture_report *report);
#endif
