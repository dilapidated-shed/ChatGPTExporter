#define _POSIX_C_SOURCE 200809L
#include "cgw_capture.h"
#include "cgw_json.h"
#include "cgw_object_store.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CGW_CAPTURE_SOURCE_IDENTITY
#define CGW_CAPTURE_SOURCE_IDENTITY "UNBOUND_HOST_FIXTURE"
#endif
enum { CAPTURE_MAX_IDS = 1024, CAPTURE_MAX_NODES = 2048, CAPTURE_MAX_REQUESTS = 512, JOURNAL_LIMIT = 4194304 };
typedef struct { char id[257], fingerprint[65], item_hash[65]; unsigned scope; } observed_id;
typedef struct { char id[257], parent[257], message_id[257]; size_t token; } graph_node;
typedef struct {
    int root, journal, lock;
    cgw_store *store;
    cgw_fetcher *fetcher;
    cgw_capture_bounds bounds;
    cgw_capture_report *report;
    observed_id *ids;
    unsigned count;
    unsigned char *response;
    size_t used;
    char journal_name[96], marker_name[160];
} capture_state;

static int hash_bytes(const void *bytes, size_t count, char output[65])
{
    unsigned char hash[32]; unsigned n = 0;
    if (!EVP_Digest(bytes, count, hash, &n, EVP_sha256(), NULL) || n != 32) return 0;
    for (unsigned i = 0; i < n; ++i) (void)snprintf(output + 2 * i, 3, "%02x", hash[i]);
    return 1;
}
static int identifier(const char *id)
{
    size_t n = strlen(id); if (!n || n > 256) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= 'A' && id[i] <= 'Z')
              || (id[i] >= '0' && id[i] <= '9') || id[i] == '-' || id[i] == '_')) return 0;
    return 1;
}
static int digest_text(const char *text)
{
    if (strlen(text) != 64) return 0;
    for (unsigned i = 0; i < 64; ++i) if (!((text[i] >= '0' && text[i] <= '9') || (text[i] >= 'a' && text[i] <= 'f'))) return 0;
    return 1;
}
static int write_all(int fd, const void *bytes, size_t length)
{
    const unsigned char *p = bytes;
    while (length) {
        ssize_t n = write(fd, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        p += (size_t)n; length -= (size_t)n;
    }
    return 1;
}
static int private_file(int fd)
{
    struct stat st;
    return fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid()
        && !(st.st_mode & 077) && st.st_nlink == 1;
}
static int reason(capture_state *s, const char *why)
{
    (void)snprintf(s->report->reason, sizeof(s->report->reason), "%s", why); return 0;
}
static int journal(capture_state *s, const char *format, ...)
{
    char line[2048]; va_list args; va_start(args, format);
    int n = vsnprintf(line, sizeof(line), format, args); va_end(args);
    struct stat st;
    if (n < 1 || n >= (int)sizeof(line) || fstat(s->journal, &st) || st.st_size > JOURNAL_LIMIT - n
        || !write_all(s->journal, line, (size_t)n) || fsync(s->journal)) return reason(s, "journal_not_durable");
    return 1;
}
static int atomic_record(capture_state *s, const char *name, const char *bytes, size_t size)
{
    /* A prior process's stage is retained. Fresh random names avoid reusing it. */
    unsigned char random[16]; int random_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    ssize_t got = random_fd >= 0 ? read(random_fd, random, sizeof(random)) : -1;
    if (random_fd >= 0) close(random_fd);
    if (got != (ssize_t)sizeof(random)) return reason(s, "stage_randomness_unavailable");
    char random_hash[65], temporary[96];
    if (!hash_bytes(random, sizeof(random), random_hash)) return 0;
    (void)snprintf(temporary, sizeof(temporary), ".capture-stage-%s", random_hash);
    int fd = openat(s->root, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_file(fd)) { if (fd >= 0) close(fd); return reason(s, "private_stage_refused"); }
    int ok = write_all(fd, bytes, size) && !fsync(fd);
    if (close(fd)) ok = 0;
    if (ok && !renameat(s->root, temporary, s->root, name) && !fsync(s->root)) return 1;
    return reason(s, "record_not_durable");
}
static int sink(void *context, const unsigned char *bytes, size_t length)
{
    capture_state *s = context;
    if (length > s->bounds.max_response_bytes - s->used || cgw_store_write(s->store, bytes, length) != CGW_STORE_OK) return 0;
    memcpy(s->response + s->used, bytes, length); s->used += length; return 1;
}
static int fetch(capture_state *s, const cgw_input *input, cgw_store_receipt *receipt)
{
    cgw_http_request plan;
    if (cgw_resolve(input, &plan) != CGW_OK) return reason(s, "request_plan_refused");
    if (s->report->requests >= s->bounds.max_requests || s->report->received_bytes >= s->bounds.max_received_bytes)
        return reason(s, "request_or_byte_ceiling");
    size_t cap = s->bounds.max_response_bytes;
    if (cap > plan.response_limit_bytes) cap = plan.response_limit_bytes;
    uint64_t remaining = s->bounds.max_received_bytes - s->report->received_bytes;
    if (remaining < cap) cap = (size_t)remaining;
    char request_hash[65];
    if (!hash_bytes(&plan, sizeof(plan), request_hash) || cgw_store_begin(s->store) != CGW_STORE_OK) return reason(s, "object_stage_refused");
    s->used = 0; ++s->report->requests;
    cgw_fetch_observation observation;
    cgw_fetch_options options = { cap, 30000 };
    cgw_fetch_result result = cgw_fetcher_execute(s->fetcher, input, options, sink, s, &observation);
    s->report->received_bytes += observation.received_bytes;
    s->report->received_chunks += observation.received_chunks;
    if (result != CGW_FETCH_OK) {
        cgw_store_abort(s->store);
        (void)journal(s, "REQUEST\t%s\t%s\t%u\t%d\n", request_hash, cgw_fetch_result_name(result), observation.http_status, observation.retryable);
        return reason(s, cgw_fetch_result_name(result));
    }
    if (cgw_store_commit(s->store, receipt) != CGW_STORE_OK) { cgw_store_abort(s->store); return reason(s, "object_commit_or_hash_failed"); }
    /* No extracted ID is trusted before these bytes and hash are durable. */
    return journal(s, "RAW\t%s\t%s\t%" PRIu64 "\n", request_hash, receipt->sha256, receipt->bytes);
}
static int read_object(capture_state *s, const char *hash)
{
    if (!digest_text(hash)) return reason(s, "invalid_object_identity");
    int objects = openat(s->root, "objects", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int fd = objects >= 0 ? openat(objects, hash, O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
    if (objects >= 0) close(objects);
    /* Immutable deduplicated objects have one link after staging cleanup. */
    if (!private_file(fd)) { if (fd >= 0) close(fd); return reason(s, "private_object_refused"); }
    s->used = 0;
    for (;;) {
        unsigned char buffer[8192]; ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return reason(s, "object_read_failed"); }
        if (!n) break;
        if ((size_t)n > s->bounds.max_response_bytes - s->used) { close(fd); return reason(s, "object_byte_ceiling"); }
        memcpy(s->response + s->used, buffer, (size_t)n); s->used += (size_t)n;
    }
    close(fd); char actual[65];
    return hash_bytes(s->response, s->used, actual) && !strcmp(actual, hash) ? 1 : reason(s, "corrupt_object_hash");
}
static int compare_ids(const void *a, const void *b) { return strcmp(((const observed_id *)a)->id, ((const observed_id *)b)->id); }
static int inventory(capture_state *s, observed_id *ids, unsigned *count, char identity[65], int record)
{
    EVP_MD_CTX *hash = EVP_MD_CTX_new();
    if (!hash || !EVP_DigestInit_ex(hash, EVP_sha256(), NULL)) { EVP_MD_CTX_free(hash); return 0; }
    *count = 0; int ok = 1;
    (void)EVP_DigestUpdate(hash, s->report->workspace_fingerprint, 64);
    for (unsigned scope = 0; scope < 2 && ok; ++scope) {
        uint64_t offset = 0, total = UINT64_MAX;
        for (;;) {
            cgw_input input = {0}; input.operation = CGW_CONVERSATION_PAGE;
            input.arguments.conversation_page.offset = offset; input.arguments.conversation_page.limit = s->bounds.page_size;
            input.arguments.conversation_page.archived = (int)scope;
            cgw_store_receipt raw;
            if (!fetch(s, &input, &raw)) { ok = 0; break; }
            (void)EVP_DigestUpdate(hash, raw.sha256, 64);
            cj_document d;
            if (!cj_parse(&d, s->response, s->used, s->bounds.max_json_work)) { ok = reason(s, "malformed_page_json"); break; }
            size_t items = cj_member(&d, 0, "items");
            uint64_t declared, echoed_offset, echoed_limit;
            if (items == CJ_MISSING || d.tokens[items].kind != CJ_ARRAY
                || !cj_uint(&d, cj_member(&d, 0, "total"), &declared)
                || !cj_uint(&d, cj_member(&d, 0, "offset"), &echoed_offset) || echoed_offset != offset
                || !cj_uint(&d, cj_member(&d, 0, "limit"), &echoed_limit) || echoed_limit != s->bounds.page_size)
                ok = reason(s, "page_schema_or_offset_changed");
            else if (declared > s->bounds.max_conversations || (total != UINT64_MAX && total != declared))
                ok = reason(s, "impossible_or_drifting_total");
            else total = declared;
            size_t missing = cj_member(&d, 0, "has_missing_conversations");
            if (ok && missing != CJ_MISSING && d.tokens[missing].kind != CJ_FALSE)
                ok = reason(s, "provider_reports_missing_conversations");
            unsigned page_count = 0;
            for (size_t item = items == CJ_MISSING ? d.count : items + 1; ok && item < d.tokens[items].next; item = d.tokens[item].next) {
                observed_id observed = {{0}, {0}, {0}, scope};
                if (++page_count > s->bounds.page_size || !cj_text(&d, cj_member(&d, item, "id"), observed.id, sizeof(observed.id))
                    || !identifier(observed.id)) { ok = reason(s, "malformed_inventory_id"); break; }
                (void)hash_bytes(observed.id, strlen(observed.id), observed.fingerprint);
                (void)hash_bytes(d.bytes + d.tokens[item].begin, d.tokens[item].end - d.tokens[item].begin, observed.item_hash);
                for (unsigned prior = 0; prior < *count; ++prior) if (!strcmp(ids[prior].id, observed.id)) {
                    ok = reason(s, strcmp(ids[prior].item_hash, observed.item_hash) ? "conflicting_inventory_id" : "duplicate_or_overlapping_id"); break;
                }
                if (!ok) break;
                if (*count >= s->bounds.max_conversations) { ok = reason(s, "inventory_ceiling"); break; }
                ids[(*count)++] = observed;
                if (record && !journal(s, "OBSERVED\t%u\t%s\t%s\n", scope, observed.id, observed.item_hash)) { ok = 0; break; }
            }
            cj_free(&d);
            if (!ok) break;
            if (offset > total || page_count > total - offset) { ok = reason(s, "items_exceed_declared_total"); break; }
            if (!page_count) { if (offset != total) ok = reason(s, "premature_empty_page"); break; }
            offset += page_count;
#ifdef CGW_MUTANT_PREMATURE_COMPLETE
            if (offset == total) break;
#endif
            /* Even reaching a declared total is followed by an empty offset
             * probe. Full-page and short-page totals receive the same check. */
        }
    }
    unsigned char digest[32]; unsigned length = 0;
    if (!ok || !EVP_DigestFinal_ex(hash, digest, &length) || length != 32) ok = 0;
    else for (unsigned i = 0; i < 32; ++i) (void)snprintf(identity + 2 * i, 3, "%02x", digest[i]);
    EVP_MD_CTX_free(hash);
    if (ok) qsort(ids, *count, sizeof(*ids), compare_ids);
    return ok;
}
static int graph_index(graph_node *nodes, unsigned count, const char *id)
{
    for (unsigned n = 0; n < count; ++n) if (!strcmp(nodes[n].id, id)) return (int)n;
    return -1;
}
static int graph(capture_state *s, const char *expected)
{
    cj_document d;
    if (!cj_parse(&d, s->response, s->used, s->bounds.max_json_work)) return reason(s, "malformed_detail_json");
    graph_node *nodes = calloc(s->bounds.max_graph_nodes, sizeof(*nodes));
    int ok = nodes != NULL; char id[257];
    size_t conversation_id = cj_member(&d, 0, "conversation_id"), plain_id = cj_member(&d, 0, "id");
    if (conversation_id == CJ_MISSING) conversation_id = plain_id;
    if (!cj_text(&d, conversation_id, id, sizeof(id)) || strcmp(id, expected)) ok = reason(s, "detail_identity_changed");
    if (plain_id != CJ_MISSING && (!cj_text(&d, plain_id, id, sizeof(id)) || strcmp(id, expected))) ok = reason(s, "conflicting_detail_identity");
    const char *pagination[] = { "has_more", "next_cursor", "next", "continuation_token" };
    for (size_t i = 0; ok && i < sizeof(pagination) / sizeof(*pagination); ++i) {
        size_t t = cj_member(&d, 0, pagination[i]);
        if (t != CJ_MISSING && d.tokens[t].kind != CJ_NULL && (i != 0 || d.tokens[t].kind != CJ_FALSE)) ok = reason(s, "detail_pagination_incomplete");
    }
    size_t mapping = cj_member(&d, 0, "mapping"); unsigned count = 0, roots = 0;
    if (mapping == CJ_MISSING || d.tokens[mapping].kind != CJ_OBJECT) ok = reason(s, "graph_schema_changed");
    for (size_t key = mapping == CJ_MISSING ? d.count : mapping + 1; ok && key < d.tokens[mapping].next; key = d.tokens[d.tokens[key].next].next) {
        size_t node = d.tokens[key].next;
        if (count >= s->bounds.max_graph_nodes) { ok = reason(s, "graph_node_ceiling"); break; }
        graph_node *n = &nodes[count]; n->token = node;
        if (!cj_text(&d, key, n->id, sizeof(n->id)) || !identifier(n->id)
            || !cj_text(&d, cj_member(&d, node, "id"), id, sizeof(id)) || strcmp(id, n->id)) { ok = reason(s, "malformed_graph_node"); break; }
        size_t parent = cj_member(&d, node, "parent");
        if (parent == CJ_MISSING) { ok = reason(s, "missing_parent_field"); break; }
        if (d.tokens[parent].kind == CJ_NULL) ++roots;
        else if (!cj_text(&d, parent, n->parent, sizeof(n->parent)) || !identifier(n->parent)) { ok = reason(s, "malformed_parent"); break; }
        size_t message = cj_member(&d, node, "message");
        if (message == CJ_MISSING || (d.tokens[message].kind != CJ_NULL && d.tokens[message].kind != CJ_OBJECT)) { ok = reason(s, "malformed_message"); break; }
        if (d.tokens[message].kind == CJ_OBJECT) {
            size_t author = cj_member(&d, message, "author"), content = cj_member(&d, message, "content");
            char role[257], content_type[257];
            if (!cj_text(&d, cj_member(&d, message, "id"), n->message_id, sizeof(n->message_id)) || !identifier(n->message_id)
                || !cj_text(&d, cj_member(&d, author, "role"), role, sizeof(role)) || !role[0]
                || !cj_text(&d, cj_member(&d, content, "content_type"), content_type, sizeof(content_type)) || !content_type[0]) { ok = reason(s, "message_schema_changed"); break; }
            for (unsigned old = 0; old < count; ++old) if (!strcmp(nodes[old].message_id, n->message_id)) { ok = reason(s, "duplicate_message_id"); break; }
            if (!ok) break;
        }
        ++count;
    }
    if (ok && (!count || roots != 1)) ok = reason(s, "graph_root_or_cycle");
    size_t work = 0;
    for (unsigned i = 0; ok && i < count; ++i) {
        int parent = nodes[i].parent[0] ? graph_index(nodes, count, nodes[i].parent) : -1;
        if (nodes[i].parent[0] && parent < 0) { ok = reason(s, "unresolved_parent"); break; }
        size_t children = cj_member(&d, nodes[i].token, "children");
        if (children == CJ_MISSING || d.tokens[children].kind != CJ_ARRAY) { ok = reason(s, "malformed_children"); break; }
        for (size_t child = children + 1; ok && child < d.tokens[children].next; child = d.tokens[child].next) {
            char child_id[257];
            if (!cj_text(&d, child, child_id, sizeof(child_id))) { ok = reason(s, "malformed_child_id"); break; }
            int index = graph_index(nodes, count, child_id); work += count;
            if (work > s->bounds.max_json_work) { ok = reason(s, "graph_work_ceiling"); break; }
            if (index < 0 || strcmp(nodes[index].parent, nodes[i].id)) { ok = reason(s, "unresolved_or_conflicting_child"); break; }
            for (size_t previous = children + 1; previous < child; previous = d.tokens[previous].next) {
                char old[257]; ++work;
                if (work > s->bounds.max_json_work) { ok = reason(s, "graph_work_ceiling"); break; }
                if (!cj_text(&d, previous, old, sizeof(old)) || !strcmp(old, child_id)) { ok = reason(s, "duplicate_child"); break; }
            }
        }
        if (ok && parent >= 0) {
            size_t siblings = cj_member(&d, nodes[parent].token, "children"); unsigned matches = 0;
            if (siblings != CJ_MISSING && d.tokens[siblings].kind == CJ_ARRAY)
                for (size_t child = siblings + 1; child < d.tokens[siblings].next; child = d.tokens[child].next) {
                    char sibling[257]; ++work;
                    if (work > s->bounds.max_json_work) { ok = reason(s, "graph_work_ceiling"); break; }
                    if (cj_text(&d, child, sibling, sizeof(sibling)) && !strcmp(sibling, nodes[i].id)) ++matches;
                }
            if (matches != 1) ok = reason(s, "nonreciprocal_parent");
        }
        int current = (int)i;
        for (unsigned steps = 0; ok && current >= 0; ++steps) {
            if (steps >= count) { ok = reason(s, "graph_cycle"); break; }
            current = nodes[current].parent[0] ? graph_index(nodes, count, nodes[current].parent) : -1; work += count;
            if (work > s->bounds.max_json_work) ok = reason(s, "graph_work_ceiling");
        }
    }
    size_t selected = cj_member(&d, 0, "current_node");
    if (ok && (selected == CJ_MISSING || !cj_text(&d, selected, id, sizeof(id)) || graph_index(nodes, count, id) < 0)) ok = reason(s, "unresolved_selected_node");
    free(nodes); cj_free(&d); return ok;
}
static void marker_name(capture_state *s, const observed_id *id)
{
    (void)snprintf(s->marker_name, sizeof(s->marker_name), "complete-%s-%s.tsv", s->report->workspace_fingerprint, id->fingerprint);
}
static int load_marker(capture_state *s, const observed_id *id, char hash[65])
{
    marker_name(s, id);
    int fd = openat(s->root, s->marker_name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (!private_file(fd)) { close(fd); return -1; }
    char bytes[512], inventory_hash[65], id_hash[65], object_hash[65], extra;
    ssize_t n = read(fd, bytes, sizeof(bytes) - 1); close(fd);
    if (n <= 0) return -1;
    bytes[n] = 0;
    if (sscanf(bytes, "CGW1\t%64s\t%64s\t%64s%c", inventory_hash, id_hash, object_hash, &extra) != 4 || extra != '\n'
        || strlen(bytes) != 5 + 64 + 1 + 64 + 1 + 64 + 1 || !digest_text(object_hash) || strcmp(id_hash, id->fingerprint)) return -1;
    if (strcmp(inventory_hash, s->report->inventory_sha256)) return 0;
    memcpy(hash, object_hash, 65); return 1;
}
static int checkpoint_detail(capture_state *s, const observed_id *id, const char *hash)
{
    if (!read_object(s, hash) || !graph(s, id->id)) return 0;
    if (!journal(s, "DETAIL\t%s\t%s\t%s\n", s->report->inventory_sha256, id->fingerprint, hash)) return 0;
    marker_name(s, id); char bytes[256];
    int n = snprintf(bytes, sizeof(bytes), "CGW1\t%s\t%s\t%s\n", s->report->inventory_sha256, id->fingerprint, hash);
    return n > 0 && n < (int)sizeof(bytes) && atomic_record(s, s->marker_name, bytes, (size_t)n)
        && journal(s, "COMPLETE\t%s\t%s\t%s\n", s->report->inventory_sha256, id->fingerprint, hash);
}
static int validate_journal(capture_state *s)
{
    if (lseek(s->journal, 0, SEEK_SET) < 0) return 0;
    unsigned char bytes[8192]; size_t count = 0; unsigned char last = '\n';
    for (;;) {
        ssize_t n = read(s->journal, bytes, sizeof(bytes));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return 0;
        if (!n) break;
        count += (size_t)n; last = bytes[n - 1];
        if (count > JOURNAL_LIMIT) return reason(s, "journal_byte_ceiling");
    }
    return last == '\n' ? 1 : reason(s, "torn_journal_retained_incomplete");
}
static int finish_report(capture_state *s, int complete)
{
    char bytes[2048], filename[96];
    (void)snprintf(filename, sizeof(filename), "validation-%s.tsv", s->report->workspace_fingerprint);
    int n = snprintf(bytes, sizeof(bytes), "schema\tcgw-capture-v1\nsource\t%s\nevidence\thost-or-declared-producer-only\nworkspace_sha256\t%s\ninventory_sha256\t%s\nexpected\t%u\ncompleted\t%u\nreused\t%u\nrequests\t%u\nreceived_bytes\t%" PRIu64 "\nmax_response_bytes\t%zu\nmax_requests\t%u\nmax_conversations\t%u\nmax_graph_nodes\t%u\nmax_json_work\t%zu\nscope_result\t%s\nreason\t%s\natomic_provider_snapshot_proven\tfalse\nfull_account_export_complete\tfalse\nprojects_shared_assets_authentication\tOUT_OF_SCOPE\n",
        CGW_CAPTURE_SOURCE_IDENTITY, s->report->workspace_fingerprint, s->report->inventory_sha256,
        s->report->expected, s->report->completed, s->report->reused, s->report->requests, s->report->received_bytes,
        s->bounds.max_response_bytes, s->bounds.max_requests, s->bounds.max_conversations, s->bounds.max_graph_nodes, s->bounds.max_json_work,
        complete ? "COMPLETE_BOUNDED_MAIN_ARCHIVED" : "INCONCLUSIVE", s->report->reason);
    return n > 0 && n < (int)sizeof(bytes) && atomic_record(s, filename, bytes, (size_t)n)
        && hash_bytes(bytes, (size_t)n, s->report->report_sha256);
}
cgw_capture_bounds cgw_capture_default_bounds(void)
{
    cgw_capture_bounds b = { 32, 256, 128, 1024, 1048576, 4000000, 33554432 }; return b;
}
cgw_capture_result cgw_capture(const char *root, const char *workspace, cgw_token_fn authorize,
                              void *authorization_context, cgw_capture_bounds bounds, cgw_capture_report *report)
{
    if (!report) return CGW_CAPTURE_REFUSED;
    memset(report, 0, sizeof(*report)); report->result = CGW_CAPTURE_REFUSED;
    capture_state s = {0}; s.root = s.journal = s.lock = -1; s.bounds = bounds; s.report = report;
    if (!root || !workspace || !identifier(workspace) || !authorize || !bounds.page_size || bounds.page_size > 100
        || !bounds.max_conversations || bounds.max_conversations > CAPTURE_MAX_IDS || !bounds.max_requests || bounds.max_requests > CAPTURE_MAX_REQUESTS
        || !bounds.max_graph_nodes || bounds.max_graph_nodes > CAPTURE_MAX_NODES || !bounds.max_response_bytes || bounds.max_response_bytes > 4194304
        || !bounds.max_json_work || bounds.max_json_work > 16000000 || !bounds.max_received_bytes) { (void)reason(&s, "invalid_capture_configuration"); return report->result; }
    (void)hash_bytes(workspace, strlen(workspace), report->workspace_fingerprint);
    s.root = open(root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (s.root < 0 || fstat(s.root, &st) || st.st_uid != geteuid() || (st.st_mode & 077)) { (void)reason(&s, "private_root_refused"); goto done; }
    char lock_name[96]; (void)snprintf(lock_name, sizeof(lock_name), "lock-%s", report->workspace_fingerprint);
    s.lock = openat(s.root, lock_name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_file(s.lock) || flock(s.lock, LOCK_EX | LOCK_NB)) { (void)reason(&s, "private_capture_lock_refused"); goto done; }
    (void)snprintf(s.journal_name, sizeof(s.journal_name), "journal-%s.tsv", report->workspace_fingerprint);
    s.journal = openat(s.root, s.journal_name, O_RDWR | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (!private_file(s.journal) || !validate_journal(&s)) goto done;
    s.store = cgw_store_open(root); s.fetcher = cgw_fetcher_open(authorize, authorization_context, workspace);
    s.response = malloc(bounds.max_response_bytes); s.ids = calloc(bounds.max_conversations, sizeof(*s.ids));
    observed_id *again = calloc(bounds.max_conversations, sizeof(*again));
    if (!s.store || !s.fetcher || !s.response || !s.ids || !again) { free(again); (void)reason(&s, "capture_resources_unavailable"); goto done; }
    report->result = CGW_CAPTURE_INCONCLUSIVE;
    unsigned second_count = 0; char second_identity[65] = {0};
    int ok = journal(&s, "BEGIN\t%s\n", CGW_CAPTURE_SOURCE_IDENTITY)
        && journal(&s, "RESUME_POLICY\treinventory_twice_then_rehash_markers\tno_automatic_retry\n")
        && inventory(&s, s.ids, &s.count, report->inventory_sha256, 1);
    report->expected = s.count;
    if (ok) ok = inventory(&s, again, &second_count, second_identity, 0);
    if (ok && (second_count != s.count || strcmp(second_identity, report->inventory_sha256))) ok = reason(&s, "inventory_drift_across_verification_pass");
    free(again);
    if (ok) ok = journal(&s, "INVENTORY\t%s\t%u\n", report->inventory_sha256, s.count);
    for (unsigned i = 0; ok && i < s.count; ++i)
        ok = journal(&s, "EXPECTED\t%s\t%s\t%s\n", report->inventory_sha256, s.ids[i].fingerprint, s.ids[i].id);
#ifdef CGW_MUTANT_SILENT_OMISSION
    if (ok && s.count) --s.count;
#endif
    for (unsigned i = 0; ok && i < s.count; ++i) {
        observed_id *id = &s.ids[i]; char hash[65] = {0};
        int marker = load_marker(&s, id, hash);
        if (marker < 0) { ok = reason(&s, "invalid_completion_marker"); break; }
        if (marker) {
#ifndef CGW_MUTANT_INVALID_MARKER
            if (!read_object(&s, hash) || !graph(&s, id->id)) { ok = 0; break; }
#endif
            ++report->reused; ++report->completed;
            if (!journal(&s, "REUSE_VERIFIED\t%s\t%s\n", id->fingerprint, hash)) ok = 0;
            continue;
        }
        cgw_input input = {0}; input.operation = CGW_CONVERSATION_DETAIL; input.arguments.detail.id = id->id;
        cgw_store_receipt raw;
        if (!fetch(&s, &input, &raw) || !graph(&s, id->id) || !checkpoint_detail(&s, id, raw.sha256)) { ok = 0; break; }
        ++report->completed;
    }
    if (ok) {
#ifndef CGW_MUTANT_INVALID_MARKER
        /* Re-read every marker and immutable object: a filename and a previous
         * success never establish completion on replay. */
        for (unsigned i = 0; ok && i < s.count; ++i) {
            char hash[65];
            if (load_marker(&s, &s.ids[i], hash) != 1 || !read_object(&s, hash) || !graph(&s, s.ids[i].id)) ok = reason(&s, "final_validation_failed");
        }
#endif
    }
    if (!journal(&s, "RESULT\t%s\t%s\n", ok ? "BOUNDED_COMPLETE" : "INCONCLUSIVE", report->reason)) ok = 0;
    if (!finish_report(&s, ok)) ok = 0;
    if (ok) report->result = CGW_CAPTURE_COMPLETE;
done:
    cgw_fetcher_close(s.fetcher); cgw_store_close(s.store); free(s.response); free(s.ids);
    if (s.journal >= 0) close(s.journal);
    if (s.lock >= 0) close(s.lock);
    if (s.root >= 0) close(s.root);
    return report->result;
}
