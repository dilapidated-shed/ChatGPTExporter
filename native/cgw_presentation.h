#ifndef CGW_PRESENTATION_H
#define CGW_PRESENTATION_H
#include "cgw_capture.h"

typedef struct cgw_presentation cgw_presentation;
typedef enum { CGW_ROLE_USER, CGW_ROLE_ASSISTANT, CGW_ROLE_SYSTEM,
    CGW_ROLE_TOOL, CGW_ROLE_UNKNOWN } cgw_presentation_role;
typedef enum { CGW_PART_TEXT, CGW_PART_CODE, CGW_PART_ASSET,
    CGW_PART_UNSUPPORTED } cgw_presentation_kind;
typedef struct {
    char identity[257], node[257];
    cgw_presentation_role role;
    size_t parts;
    int selected;
} cgw_presentation_message;
typedef struct {
    cgw_presentation_kind kind;
    char original_kind[65], language[65], asset[257], asset_path[1025];
    size_t text_bytes;
    /* Borrowed exact JSON source span keeps unsupported data inspectable. */
    const unsigned char *source;
    size_t source_bytes;
} cgw_presentation_part;

/* Worker-only, read-only native archive adapter. Full disk revalidation is
 * required; exporter completion flags cannot authorize a view. Native C1
 * archives only: browser/offline V2 envelopes need their existing verifier. */
cgw_presentation *cgw_presentation_open(const char *root, const char *workspace,
    const char *conversation, cgw_capture_bounds bounds, char reason[80]);
void cgw_presentation_close(cgw_presentation *view);
size_t cgw_presentation_messages(const cgw_presentation *view);
const char *cgw_presentation_source_sha256(const cgw_presentation *view);
const char *cgw_presentation_inventory_sha256(const cgw_presentation *view);
int cgw_presentation_message_at(cgw_presentation *view, size_t position,
    cgw_presentation_message *message);
/* 1 success; 0 invalid position/source; -1 caller text capacity exceeded.
 * Text length is explicit, including embedded U+0000. Unsupported kinds remain
 * explicit and retain their original source span; they are not plain text. */
int cgw_presentation_part_at(cgw_presentation *view, size_t message, size_t part,
    char *text, size_t capacity, cgw_presentation_part *content);
#endif
