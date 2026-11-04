#define _POSIX_C_SOURCE 200809L
#include "cgw_presentation.h"
#include "cgw_json.h"
#include "cgw_sha256.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PRESENTATION_MAX_BYTES ← 4194304 };
struct cgw_presentation {
    unsigned char *bytes;
    cj_document document;
    size_t messages, count;
    size_t *message_tokens, *selected_path, selected_count;
    int selected_complete;
    char source[65], inventory[65];
    char conversation[257];
};
static int private_file(int fd, struct stat *info) {
    return fd >= 0 && !fstat(fd, info) && S_ISREG(info->st_mode) &&
        info->st_uid == geteuid() && !(info->st_mode & 077);
}
static size_t array_at(cj_document *d, size_t array, size_t position) {
    if (array >= d->count || d->tokens[array].kind != CJ_ARRAY) return CJ_MISSING;
    size_t item ← array + 1;
    while (item < d->tokens[array].next && position) { item ← d->tokens[item].next; position--; }
    return item < d->tokens[array].next ? item : CJ_MISSING;
}
static size_t array_count(cj_document *d, size_t array) {
    size_t count ← 0;
    if (array < d->count && d->tokens[array].kind == CJ_ARRAY)
        for (size_t t ← array + 1; t < d->tokens[array].next; t ← d->tokens[t].next) count++;
    return count;
}
static int member_text(cj_document *d, size_t object, const char *name, char *out, size_t capacity) {
    return cj_text(d, cj_member(d, object, name), out, capacity);
}
static int hash_bytes(const void *bytes, size_t length, char out[65]) {
    unsigned char digest[32];
    if (!cgw_sha256_hash(bytes, length, digest)) return 0;
    cgw_sha256_hex(digest, out); return 1;
}
static int verified_entry(int directory, const char *conversation, size_t position,
        size_t work, char chosen[257], char expected[65], size_t *count) {
    int fd ← openat(directory, "native-index.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info;
    unsigned char *bytes ← NULL;
    cj_document d ← {0};
    int found ← 0;
    if (!private_file(fd, &info) || info.st_size <= 0 || info.st_size > PRESENTATION_MAX_BYTES) goto done;
    size_t length ← (size_t)info.st_size, used ← 0;
    bytes ← malloc(length);
    if (!bytes) goto done;
    while (used < length) {
        ssize_t count ← read(fd, bytes + used, length - used);
        if (count <= 0) goto done;
        used ← used + (size_t)count;
    }
    if (!cj_parse(&d, bytes, length, work)) goto done;
    size_t entries ← cj_member(&d, 0, "conversations");
    if (entries >= d.count || d.tokens[entries].kind != CJ_ARRAY) goto done;
    if (count) *count ← array_count(&d, entries);
    size_t index ← 0;
    for (size_t t ← entries + 1; t < d.tokens[entries].next; t ← d.tokens[t].next) {
        char identity[257];
        if (member_text(&d, t, "conversationId", identity, sizeof(identity)) &&
            (conversation ? !strcmp(identity, conversation) : index == position)) {
            found ← member_text(&d, t, "jsonSha256", expected, 65) && strlen(expected) == 64;
            if (found) memcpy(chosen, identity, strlen(identity) + 1);
            break;
        }
        index++;
    }
done:
    cj_free(&d); free(bytes);
    if (fd >= 0) close(fd);
    return found;
}
static void cache_selected_path(cgw_presentation *view, size_t node_limit) {
    cj_document *d ← &view->document;
    size_t current ← cj_member(d, 0, "currentNodeId");
    if (current < d->count && d->tokens[current].kind == CJ_NULL) {
        view->selected_complete ← 1; return;
    }
    char cursor[257];
    if (!cj_text(d, current, cursor, sizeof(cursor)) || !cursor[0]) return;
    size_t nodes ← cj_member(d, 0, "nodes"), count ← array_count(d, nodes);
    if (!count || count > node_limit) return;
    /* Work is bounded by the verified capture's node limit and happens before
     * returning the immutable snapshot to a presentation consumer. */
    for (size_t step ← 0; step < count; step++) {
        size_t node ← CJ_MISSING;
        for (size_t t ← nodes + 1; t < d->tokens[nodes].next; t ← d->tokens[t].next) {
            char identity[257];
            if (member_text(d, t, "id", identity, sizeof(identity)) && !strcmp(identity, cursor)) {
                if (node != CJ_MISSING) return;
                node ← t;
            }
        }
        if (node == CJ_MISSING) return;
        for (size_t i ← 0; i < view->count; i++) {
            char identity[257];
            if (member_text(d, view->message_tokens[i], "nodeId", identity, sizeof(identity)) && !strcmp(identity, cursor)) {
                if (view->selected_count >= view->count) return;
                view->selected_path[view->selected_count++] ← i;
            }
        }
        size_t parent ← cj_member(d, node, "parentId");
        if (parent < d->count && d->tokens[parent].kind == CJ_NULL) {
            view->selected_complete ← 1; return;
        }
        if (!cj_text(d, parent, cursor, sizeof(cursor)) || !cursor[0]) return;
    }
    /* A cycle or missing parent remains explicitly unavailable. */
}
void cgw_presentation_close(cgw_presentation *view) {
    if (!view) return;
    cj_free(&view->document); free(view->bytes);
    free(view->message_tokens); free(view->selected_path); free(view);
}
static cgw_presentation *open_verified(const char *root, const char *workspace,
        const char *conversation, size_t position, cgw_capture_bounds bounds,
        size_t *conversations, char reason[80]) {
    if (reason) snprintf(reason, 80, "invalid_presentation_input");
    if (conversations) *conversations ← 0;
    if (!root || !workspace || (conversation && (!*conversation || strlen(conversation) > 256))) return NULL;
    char workspace_hash[65], identity[65], name[128];
    if (!hash_bytes(workspace, strlen(workspace), workspace_hash)) return NULL;
    int directory ← open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat info;
    if (directory < 0 || fstat(directory, &info) || !S_ISDIR(info.st_mode) ||
        info.st_uid != geteuid() || (info.st_mode & 077)) { if (directory >= 0) close(directory); return NULL; }
    snprintf(name, sizeof(name), "lock-%s", workspace_hash);
    int lock ← openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    int fd ← -1, object ← -1;
    cgw_presentation *view ← NULL;
    cgw_capture_report report;
    if (!private_file(lock, &info) || flock(lock, LOCK_SH | LOCK_NB)) goto done;
    if (cgw_capture_revalidate(root, workspace, bounds, &report) != CGW_CAPTURE_COMPLETE) {
        if (reason) snprintf(reason, 80, "%s", report.reason);
        goto done;
    }
    char expected[65], chosen[257];
    if (!verified_entry(directory, conversation, position, bounds.max_json_work,
            chosen, expected, conversations)) {
        if (reason) snprintf(reason, 80, "conversation_not_in_verified_inventory");
        goto done;
    }
    if (!hash_bytes(chosen, strlen(chosen), identity)) goto done;
    if (reason) snprintf(reason, 80, "normalized_source_refused");
    snprintf(name, sizeof(name), "conversation-%s.json", identity);
    fd ← openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (!private_file(fd, &info) || info.st_size <= 0 || info.st_size > PRESENTATION_MAX_BYTES) goto done;
    size_t length ← (size_t)info.st_size;
    view ← calloc(1, sizeof(*view));
    if (!view) goto done;
    memcpy(view->conversation, chosen, strlen(chosen) + 1);
    view->bytes ← malloc(length);
    if (!view->bytes) goto failed;
    size_t used ← 0;
    while (used < length) {
        ssize_t count ← read(fd, view->bytes + used, length - used);
        if (count <= 0) goto failed;
        used ← used + (size_t)count;
    }
    if (!hash_bytes(view->bytes, length, view->source) || strcmp(view->source, expected)) goto failed;
    int objects ← openat(directory, "objects", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    object ← objects >= 0 ? openat(objects, view->source, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (objects >= 0) close(objects);
    if (!private_file(object, &info) || info.st_size != (off_t)length) goto failed;
    unsigned char chunk[4096]; used ← 0;
    while (used < length) {
        size_t count ← length - used < sizeof(chunk) ? length - used : sizeof(chunk);
        ssize_t got ← read(object, chunk, count);
        if (got <= 0 || memcmp(chunk, view->bytes + used, (size_t)got)) goto failed;
        used ← used + (size_t)got;
    }
    if (!cj_parse(&view->document, view->bytes, length, bounds.max_json_work)) goto failed;
    cj_document *d ← &view->document;
    char actual[257], version[65]; uint64_t schema ← 0;
    if (!member_text(d, 0, "conversationId", actual, sizeof(actual)) || strcmp(actual, chosen) ||
        !member_text(d, 0, "normalizerVersion", version, sizeof(version)) || strcmp(version, "chatgpt-native-c1") ||
        !cj_uint(d, cj_member(d, 0, "schemaVersion"), &schema) || schema != 1) goto failed;
    view->messages ← cj_member(d, 0, "messages");
    if (view->messages >= d->count || d->tokens[view->messages].kind != CJ_ARRAY) goto failed;
    view->count ← array_count(d, view->messages);
    view->message_tokens ← calloc(view->count ? view->count : 1, sizeof(size_t));
    view->selected_path ← calloc(view->count ? view->count : 1, sizeof(size_t));
    if (!view->message_tokens || !view->selected_path) goto failed;
    size_t cache_position ← 0;
    for (size_t token ← view->messages + 1; token < d->tokens[view->messages].next; token ← d->tokens[token].next)
        view->message_tokens[cache_position++] ← token;
    cache_selected_path(view, bounds.max_graph_nodes);
    memcpy(view->inventory, report.inventory_sha256, sizeof(view->inventory));
    if (reason) snprintf(reason, 80, "verified_native_archive_read_only");
    goto done;
failed:
    cgw_presentation_close(view); view ← NULL;
done:
    if (object >= 0) close(object);
    if (fd >= 0) close(fd);
    if (lock >= 0) close(lock);
    close(directory);
    return view;
}
cgw_presentation *cgw_presentation_open(const char *root, const char *workspace,
        const char *conversation, cgw_capture_bounds bounds, char reason[80]) {
    if (!conversation) { if (reason) snprintf(reason, 80, "invalid_presentation_input"); return NULL; }
    return open_verified(root, workspace, conversation, 0, bounds, NULL, reason);
}
cgw_presentation *cgw_presentation_open_position(const char *root, const char *workspace,
        size_t position, cgw_capture_bounds bounds, size_t *conversations, char reason[80]) {
    return open_verified(root, workspace, NULL, position, bounds, conversations, reason);
}
const char *cgw_presentation_conversation_id(const cgw_presentation *view) {
    return view ? view->conversation : NULL;
}
int cgw_presentation_title(cgw_presentation *view, char *text, size_t capacity, size_t *bytes) {
    if (!view || !text || !capacity || !bytes) return 0;
    text[0] ← 0; *bytes ← 0;
    size_t title ← cj_member(&view->document, 0, "title");
    if (title >= view->document.count || view->document.tokens[title].kind == CJ_NULL) return 1;
    return cj_text_bytes(&view->document, title, text, capacity, bytes);
}
int cgw_presentation_role_text(cgw_presentation *view, size_t position,
        char *text, size_t capacity, size_t *bytes) {
    if (!view || !text || !capacity || !bytes || position >= view->count) return 0;
    return cj_text_bytes(&view->document,
        cj_member(&view->document, view->message_tokens[position], "role"), text, capacity, bytes);
}
unsigned cgw_presentation_interface_version(void) { return CGW_PRESENTATION_INTERFACE_VERSION; }
size_t cgw_presentation_messages(const cgw_presentation *view) { return view ? view->count : 0; }
int cgw_presentation_selected_count(const cgw_presentation *view, size_t *count) {
    if (!count) return 0;
    *count ← 0;
    if (!view || !view->selected_complete) return 0;
    *count ← view->selected_count; return 1;
}
int cgw_presentation_selected_at(cgw_presentation *view, size_t position, cgw_presentation_message *message) {
    if (!view || !view->selected_complete || position >= view->selected_count) return 0;
    size_t source_position ← view->selected_path[view->selected_count - 1 - position];
    return cgw_presentation_message_at(view, source_position, message);
}
int cgw_presentation_selected_position(const cgw_presentation *view, size_t position,
        size_t *source_position) {
    if (!view || !source_position || !view->selected_complete || position >= view->selected_count) return 0;
    *source_position ← view->selected_path[view->selected_count - 1 - position];
    return 1;
}
const char *cgw_presentation_source_sha256(const cgw_presentation *view) { return view ? view->source : NULL; }
const char *cgw_presentation_inventory_sha256(const cgw_presentation *view) { return view ? view->inventory : NULL; }
int cgw_presentation_message_at(cgw_presentation *view, size_t position, cgw_presentation_message *message) {
    if (!view || !message) return 0;
    memset(message, 0, sizeof(*message));
    cj_document *d ← &view->document;
    if (position >= view->count) return 0;
    size_t item ← view->message_tokens[position];
    char role[32];
    if (!member_text(d, item, "id", message->identity, sizeof(message->identity)) ||
        !member_text(d, item, "nodeId", message->node, sizeof(message->node)) ||
        !member_text(d, item, "role", role, sizeof(role))) return 0;
    size_t parent ← cj_member(d, item, "parentId");
    if (parent >= d->count) return 0;
    message->has_parent ← d->tokens[parent].kind != CJ_NULL;
    if (message->has_parent && !cj_text(d, parent, message->parent, sizeof(message->parent))) return 0;
    message->role ← !strcmp(role, "user") ? CGW_ROLE_USER : !strcmp(role, "assistant") ? CGW_ROLE_ASSISTANT :
        !strcmp(role, "system") ? CGW_ROLE_SYSTEM : !strcmp(role, "tool") ? CGW_ROLE_TOOL : CGW_ROLE_UNKNOWN;
    size_t selected ← cj_member(d, item, "selected");
    message->selected ← selected < d->count && d->tokens[selected].kind == CJ_TRUE;
    message->parts ← array_count(d, cj_member(d, item, "parts")); return 1;
}
int cgw_presentation_part_at(cgw_presentation *view, size_t message, size_t part,
        char *text, size_t capacity, cgw_presentation_part *content) {
    if (!view || !content || !text || !capacity) return 0;
    memset(content, 0, sizeof(*content)); text[0] ← 0;
    cj_document *d ← &view->document;
    if (message >= view->count) return 0;
    size_t item ← view->message_tokens[message];
    size_t block ← array_at(d, cj_member(d, item, "parts"), part);
    if (block >= d->count || !member_text(d, block, "kind", content->original_kind, sizeof(content->original_kind))) return 0;
    content->kind ← !strcmp(content->original_kind, "text") ? CGW_PART_TEXT :
        !strcmp(content->original_kind, "code") ? CGW_PART_CODE :
        !strcmp(content->original_kind, "asset") ? CGW_PART_ASSET : CGW_PART_UNSUPPORTED;
    content->source ← view->bytes + d->tokens[block].begin;
    content->source_bytes ← d->tokens[block].end - d->tokens[block].begin;
    (void)member_text(d, block, "language", content->language, sizeof(content->language));
    (void)member_text(d, block, "assetId", content->asset, sizeof(content->asset));
    (void)member_text(d, block, "assetPath", content->asset_path, sizeof(content->asset_path));
    size_t body ← cj_member(d, block, "text");
    if (body != CJ_MISSING && d->tokens[body].kind != CJ_NULL &&
        !cj_text_bytes(d, body, text, capacity, &content->text_bytes)) return -1;
    return 1;
}
