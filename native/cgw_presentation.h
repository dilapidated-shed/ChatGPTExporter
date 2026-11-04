#ifndef CGW_PRESENTATION_H
#define CGW_PRESENTATION_H
#include "cgw_capture.h"
#define CGW_PRESENTATION_INTERFACE_VERSION 1u

typedef struct cgw_presentation cgw_presentation;
typedef enum { CGW_ROLE_USER, CGW_ROLE_ASSISTANT, CGW_ROLE_SYSTEM,
    CGW_ROLE_TOOL, CGW_ROLE_UNKNOWN } cgw_presentation_role;
typedef enum { CGW_PART_TEXT, CGW_PART_CODE, CGW_PART_ASSET,
    CGW_PART_UNSUPPORTED } cgw_presentation_kind;
typedef struct {
    char identity[257], node[257], parent[257];
    cgw_presentation_role role;
    size_t parts;
    int selected, has_parent;
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
/* Select a conversation in the verified native inventory's canonical order.
 * This order is explicitly not message chronology. No second index is stored. */
cgw_presentation *cgw_presentation_open_position(const char *root, const char *workspace,
    size_t position, cgw_capture_bounds bounds, size_t *conversations, char reason[80]);
const char *cgw_presentation_conversation_id(const cgw_presentation *view);
int cgw_presentation_title(cgw_presentation *view, char *text, size_t capacity, size_t *bytes);
int cgw_presentation_role_text(cgw_presentation *view, size_t position,
    char *text, size_t capacity, size_t *bytes);
void cgw_presentation_close(cgw_presentation *view);
unsigned cgw_presentation_interface_version(void);
size_t cgw_presentation_messages(const cgw_presentation *view);
/* Canonical inventory order is not conversation order. A proved selected path
 * is derived from currentNodeId and actual parent links during worker-open.
 * count returns 0 if the path is incomplete; it never invents a chronology.
 * All branch messages remain inspectable through message_at. */
int cgw_presentation_selected_count(const cgw_presentation *view, size_t *count);
int cgw_presentation_selected_at(cgw_presentation *view, size_t position,
    cgw_presentation_message *message);
int cgw_presentation_selected_position(const cgw_presentation *view, size_t position,
    size_t *source_position);
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
