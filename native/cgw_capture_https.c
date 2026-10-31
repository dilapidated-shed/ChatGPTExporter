/* Legacy native HTTPS adapter remains for qualified Linux/loopback runs.
 * Android links the transport-neutral capture coordinator without this file. */
#include "cgw_capture.h"
#include <string.h>

typedef struct {
    cgw_fetcher *fetcher;
} https_context;

static cgw_fetch_result execute_https(void *context, const cgw_input *input,
                                      cgw_fetch_options options, cgw_chunk_fn sink,
                                      void *sink_context, cgw_fetch_observation *observation)
{
    https_context *ctx = context;
    return cgw_fetcher_execute(ctx->fetcher, input, options, sink, sink_context,
                               observation);
}

cgw_capture_result cgw_capture(const char *root, const char *workspace,
                               cgw_token_fn authorize, void *authorization_context,
                               cgw_capture_bounds bounds, cgw_capture_report *report)
{
    if (!authorize || !workspace) {
        if (report) {
            memset(report, 0, sizeof(*report));
            report->result = CGW_CAPTURE_REFUSED;
            strcpy(report->reason, "invalid_capture_configuration");
        }
        return CGW_CAPTURE_REFUSED;
    }
    https_context ctx = { cgw_fetcher_open(authorize, authorization_context, workspace) };
    if (!ctx.fetcher) {
        if (report) {
            memset(report, 0, sizeof(*report));
            report->result = CGW_CAPTURE_REFUSED;
            strcpy(report->reason, "capture_resources_unavailable");
        }
        return CGW_CAPTURE_REFUSED;
    }
    cgw_capture_result result = cgw_capture_with_transport(
        root, workspace, execute_https, &ctx, bounds, report);
    cgw_fetcher_close(ctx.fetcher);
    return result;
}
