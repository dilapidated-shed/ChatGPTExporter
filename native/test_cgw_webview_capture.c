#define _POSIX_C_SOURCE 200809L
#include "cgw_webview_capture.h"
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum scenario { GOOD, EXPIRED, NAVIGATE, TRUNCATED, INCOMPLETE, FORGED, TOO_LARGE };
typedef struct { enum scenario scenario; unsigned calls; } fixture;
static int failed, assertions;

static void check(int value, const char *label)
{
    ++assertions;
    if (!value) { ++failed; fprintf(stderr, "FAIL %s\n", label); }
}
static int deterministic_fixture_entropy(void *context, unsigned char *buffer, size_t size)
{
    (void)context;
    for (size_t i = 0; i < size; ++i) buffer[i] = (unsigned char)(i + 7);
    return 1; /* Only the offline synthetic host, never production entropy. */
}
static size_t encode(const unsigned char *source, size_t length, char *output)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t n = 0;
    for (size_t i = 0; i < length; i += 3) {
        unsigned v = (unsigned)source[i] << 16;
        if (i + 1 < length) v |= (unsigned)source[i + 1] << 8;
        if (i + 2 < length) v |= (unsigned)source[i + 2];
        output[n++] = alphabet[(v >> 18) & 63];
        output[n++] = alphabet[(v >> 12) & 63];
        if (i + 1 < length) output[n++] = alphabet[(v >> 6) & 63];
        if (i + 2 < length) output[n++] = alphabet[v & 63];
    }
    output[n] = 0;
    return n;
}
static cgw_webview_result reply(cgw_webview_bridge *bridge, const cgw_webview_command *command,
                                unsigned status, const char *body)
{
    char wire[CGW_WEBVIEW_MESSAGE_CAPACITY];
    if (status != 200) {
        int n = snprintf(wire, sizeof(wire), "CGW1;reply;%s;%" PRIu64 ";%u;0;1;",
                         command->nonce, command->request_id, status);
        return cgw_webview_bridge_receive(bridge, wire, (size_t)n);
    }
    size_t length = strlen(body), position = 0;
    uint64_t sequence = 0;
    while (position < length) {
        size_t size = length - position;
        if (size > 2048) size = 2048;
        char encoded[3000];
        (void)encode((const unsigned char *)body + position, size, encoded);
        int n = snprintf(wire, sizeof(wire), "CGW1;reply;%s;%" PRIu64 ";200;%" PRIu64 ";%d;%s",
                         command->nonce, command->request_id, sequence,
                         position + size == length, encoded);
        cgw_webview_result r = cgw_webview_bridge_receive(bridge, wire, (size_t)n);
        if (r != CGW_WEBVIEW_OK) return r;
        position += size; ++sequence;
    }
    return CGW_WEBVIEW_OK;
}
static cgw_webview_result exchange(void *context, const cgw_webview_command *command,
                                   cgw_webview_bridge *bridge, unsigned timeout_ms)
{
    fixture *f = context; ++f->calls;
    if (timeout_ms < 1) return CGW_WEBVIEW_INVALID_ARGUMENT;
    if (strstr(command->wire, "Bearer ") || strstr(command->wire, "Cookie:")) return CGW_WEBVIEW_REQUEST_REJECTED;
    if (f->scenario == EXPIRED) return reply(bridge, command, 401, "");
    if (f->scenario == NAVIGATE) return cgw_webview_bridge_navigation_started(bridge);
    if (f->scenario == FORGED) {
        char bad[256]; int n = snprintf(bad, sizeof(bad),
            "CGW1;reply;00000000000000000000000000000000;%" PRIu64 ";200;0;1;e30",
            command->request_id);
        return cgw_webview_bridge_receive(bridge, bad, (size_t)n);
    }
    const char *path = command->wire;
    for (unsigned i = 0; i < 5; ++i) {
        path = strchr(path, ';'); if (!path) return CGW_WEBVIEW_INVALID_WIRE;
        ++path;
    }
    char endpoint[8192] = {0};
    const char *end = strchr(path, ';');
    if (!end || (size_t)(end - path) >= sizeof(endpoint)) return CGW_WEBVIEW_INVALID_WIRE;
    memcpy(endpoint, path, (size_t)(end - path));
    char body[4096];
    if (!strncmp(endpoint, "/backend-api/conversations?", 27)) {
        uint64_t offset = UINT64_MAX;
        unsigned limit = 0;
        if (sscanf(endpoint, "/backend-api/conversations?offset=%" SCNu64 "&limit=%u", &offset, &limit) != 2)
            return CGW_WEBVIEW_INVALID_WIRE;
        int archived = strstr(endpoint, "is_archived=true") != NULL;
        unsigned total = archived ? (f->scenario == INCOMPLETE ? 1u : 0u) : 1u;
        if (f->scenario == TRUNCATED) return reply(bridge, command, 200, "{\"items\":[");
        (void)snprintf(body, sizeof(body),
            "{\"offset\":%" PRIu64 ",\"limit\":%u,\"total\":%u,\"items\":[%s]}",
            offset, limit, total, !archived && offset == 0
                ? "{\"id\":\"conversation-1\",\"title\":\"stable\"}" : "");
        return reply(bridge, command, 200, body);
    }
    if (!strcmp(endpoint, "/backend-api/conversation/conversation-1")) {
        FILE *file = fopen("fixtures/capture/conversation-1.json", "rb");
        if (!file) return CGW_WEBVIEW_INVALID_ARGUMENT;
        size_t n = fread(body, 1, sizeof(body) - 1, file);
        fclose(file); body[n] = 0;
        if (f->scenario == TOO_LARGE) {
            char larger[8192]; memset(larger, 'x', sizeof(larger) - 1);
            larger[sizeof(larger) - 1] = 0;
            return reply(bridge, command, 200, larger);
        }
        return reply(bridge, command, 200, body);
    }
    return reply(bridge, command, 404, "");
}
static cgw_capture_report run(const char *root, enum scenario which)
{
    cgw_webview_bridge_config cfg = { deterministic_fixture_entropy, NULL, 16384 };
    cgw_webview_bridge *bridge = cgw_webview_bridge_open(&cfg);
    check(bridge != NULL, "open isolated page bridge");
    check(cgw_webview_bridge_document_committed(bridge, "https://chatgpt.com", 1) == CGW_WEBVIEW_OK, "verify main document");
    check(cgw_webview_bridge_port_ready(bridge) == CGW_WEBVIEW_OK, "verify page port");
    fixture f = { which, 0 };
    cgw_webview_capture_transport transport = { bridge, exchange, &f };
    cgw_capture_bounds b = cgw_capture_default_bounds();
    b.page_size = 2; b.max_conversations = 8; b.max_requests = 32;
    b.max_response_bytes = 16384; b.max_received_bytes = 131072;
    if (which == TOO_LARGE) b.max_response_bytes = 256;
    cgw_capture_report report = {0};
    (void)cgw_capture_with_transport(root, "selected_fixture_workspace",
            cgw_webview_capture_fetch, &transport, b, &report);
    check(f.calls == report.requests, "one page-port exchange per recorded request");
    cgw_webview_bridge_close(bridge);
    return report;
}
static int new_root(char buffer[128])
{
    (void)snprintf(buffer, 128, "build/webcapture-XXXXXX");
    return mkdtemp(buffer) != NULL;
}
static int find_detail(const char *root)
{
    char directory[256]; (void)snprintf(directory, sizeof(directory), "%s/objects", root);
    DIR *entries = opendir(directory);
    if (!entries) return 0;
    struct dirent *item; int found = 0;
    while ((item = readdir(entries))) {
        if (item->d_name[0] == '.') continue;
        char path[512]; (void)snprintf(path, sizeof(path), "%s/%s", directory, item->d_name);
        FILE *in = fopen(path, "rb"); if (!in) continue;
        char bytes[4096] = {0}; size_t n = fread(bytes, 1, sizeof(bytes) - 1, in); fclose(in);
        if (n && strstr(bytes, "\"future_unknown\"") && strstr(bytes, "\"assistant-alt\"")
              && strstr(bytes, "\"future-role\"")) {
            FILE *expected = fopen("fixtures/capture/conversation-1.json", "rb");
            char oracle[4096] = {0};
            size_t length = expected ? fread(oracle, 1, sizeof(oracle) - 1, expected) : 0;
            if (expected) fclose(expected);
            if (n == length && memcmp(bytes, oracle, n) == 0) ++found;
        }
        struct stat st; check(!stat(path, &st) && S_ISREG(st.st_mode) && !(st.st_mode & 077),
                               "private raw immutable object");
    }
    closedir(entries); return found == 1;
}
int main(void)
{
    (void)umask(077);
    char root[128];
    check(new_root(root), "private root created");
    cgw_capture_report good = run(root, GOOD);
    check(good.result == CGW_CAPTURE_COMPLETE && good.expected == 1 && good.completed == 1,
          "page-world transport completes main and archived validation");
    check(good.requests == 7 && !good.full_account_export_complete,
          "two exact inventory passes and bounded-only report");
    check(find_detail(root), "raw provider bytes preserved with unknown fields and alternate branch");
    cgw_capture_report resumed = run(root, GOOD);
    check(resumed.result == CGW_CAPTURE_COMPLETE && resumed.reused == 1 && resumed.requests == 6,
          "restart revalidates persisted immutable object and marker");
    enum scenario failures[] = { EXPIRED, NAVIGATE, TRUNCATED, INCOMPLETE, FORGED, TOO_LARGE };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        char other[128]; check(new_root(other), "separate failed-capture root");
        cgw_capture_report bad = run(other, failures[i]);
        check(bad.result != CGW_CAPTURE_COMPLETE && !bad.full_account_export_complete,
              "bad session/navigation/incomplete response cannot complete");
        check(bad.completed == 0, "no completion marker on failure");
    }
    printf("webview_capture_assertions=%d failures=%d\n", assertions, failed);
    return failed ? 1 : 0;
}
