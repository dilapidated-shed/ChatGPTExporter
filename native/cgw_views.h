#ifndef CGW_VIEWS_H
#define CGW_VIEWS_H
#include <stddef.h>
typedef struct { char *json, *markdown; size_t json_bytes, markdown_bytes; } cgw_views;
/* Graph validity and listing identity must already be independently checked.
 * No filesystem, platform, network or credential dependency. Returns zero on
 * schema/work/output ceilings; preserves unknown data in raw/extension fields.
 * Workspace identity follows chatgpt-web-workspace-v1, independently of the
 * legacy native journal's internal workspace SHA-256 namespace. */
int cgw_views_normalize(const unsigned char *detail, size_t detail_bytes,
    const unsigned char *listing, size_t listing_bytes, const char *workspace_id,
    unsigned archived, size_t work_limit, cgw_views *output);
void cgw_views_free(cgw_views *views);
#endif
