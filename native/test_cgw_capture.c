#define _POSIX_C_SOURCE 200809L
#ifndef CGW_TEST_LOCAL_TLS
#error This entrypoint may only run against the synthetic loopback provider.
#endif
#include "cgw_capture.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum scenario { GOOD, EMPTY, DUPLICATE, OVERLAP, CONFLICT, REPEATED_OFFSET, TOTAL_DRIFT,
    PREMATURE_EMPTY, IMPOSSIBLE_TOTAL, INVENTORY_DRIFT, TRUNCATED, GRAPH_CYCLE, MISSING_PARENT,
    DETAIL_PAGINATION, AUTH401, AUTH403, RATE429, SERVER503, OVERSIZED, SECRET_ECHO, SHORT_TOTAL, STOP_SECOND, MALFORMED_GRAPH, DUPLICATE_KEY };
static const char token[] = "synthetic-fixture-token-123456";
static const char workspace[] = "selected_fixture_workspace";
static unsigned assertions, failures;
static int killed_expected_mutant;
static SSL_CTX *server_context;

static void check(int condition, const char *name)
{
    ++assertions;
    if (!condition) { ++failures; fprintf(stderr, "FAIL %s\n", name); }
}
static int authorize(void *context, char *buffer, size_t capacity, size_t *length)
{
    (void)context;
    if (capacity < sizeof(token)) return 0;
    memcpy(buffer, token, sizeof(token)); *length = sizeof(token) - 1; return 1;
}
static int ssl_write_all(SSL *ssl, const char *text, size_t size)
{
    while (size) {
        int n = SSL_write(ssl, text, size > 8192 ? 8192 : (int)size);
        if (n <= 0) return 0;
        text += n; size -= (size_t)n;
    }
    return 1;
}
static void respond(SSL *ssl, unsigned status, const char *body, int split)
{
    char header[256]; size_t size = strlen(body);
    int n = snprintf(header, sizeof(header), "HTTP/1.1 %u Fixture\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", status, size);
    if (n < 1 || !ssl_write_all(ssl, header, (size_t)n)) return;
    if (!split) (void)ssl_write_all(ssl, body, size);
    else for (size_t i = 0; i < size; ++i) {
        if (!ssl_write_all(ssl, body + i, 1)) break;
        struct timespec pause = {0, 1000000}; (void)nanosleep(&pause, NULL);
    }
}
static void detail(char *body, size_t capacity, const char *id, enum scenario mode)
{
    /* Independent oracle: root → user → two sibling assistants. Three message
     * objects include a future role. No alternating-role reconstruction. The
     * root/user/assistant naming and provider fields follow the retained TS
     * tests/fixtures/chatgpt.ts contract; unknown fields stay in these bytes. */
    (void)snprintf(body, capacity,
        "{\"id\":\"%.256s\",\"title\":\"Synthetic conversation\",\"create_time\":null,\"update_time\":null,\"current_node\":\"assistant-1\","
        "\"future_unknown\":{\"preserve\":[1,\"λ\",null]},\"has_more\":%s,\"mapping\":{"
        "\"root-1\":{\"id\":\"root-1\",\"parent\":%s,\"children\":[\"user-1\"],\"message\":null},"
        "\"user-1\":{\"id\":\"user-1\",\"parent\":\"%s\",\"children\":[\"assistant-1\",\"assistant-alt\"],"
        "\"message\":{\"id\":\"message-user-1\",\"author\":{\"role\":\"user\"},\"content\":{\"content_type\":\"text\",\"parts\":[\"Hello from a synthetic fixture.\"]}}},"
        "\"assistant-1\":{\"id\":\"assistant-1\",\"parent\":\"user-1\",\"children\":%s,"
        "\"message\":{\"id\":\"message-assistant-1\",\"author\":{\"role\":\"assistant\"},\"content\":{\"content_type\":\"text\",\"parts\":[\"Synthetic response.\"]}}},"
        "\"assistant-alt\":{\"id\":\"assistant-alt\",\"parent\":\"user-1\",\"children\":[],"
        "\"message\":{\"id\":\"message-future\",\"author\":{\"role\":\"future-role\"},\"content\":{\"content_type\":\"future\",\"future_part\":true}}}}}\n",
        id, mode == DETAIL_PAGINATION ? "true" : "false", mode == GRAPH_CYCLE ? "\"assistant-1\"" : "null",
        mode == MISSING_PARENT ? "absent-parent" : "root-1", mode == GRAPH_CYCLE ? "[\"root-1\"]" : "[]");
}
static void serve_request(SSL *ssl, enum scenario mode, unsigned *first_pages, int event_fd)
{
    char request[16384]; size_t used = 0;
    while (used + 1 < sizeof(request)) {
        int n = SSL_read(ssl, request + used, (int)(sizeof(request) - used - 1));
        if (n <= 0) return;
        used += (size_t)n; request[used] = 0;
        if (strstr(request, "\r\n\r\n")) break;
    }
    char required[160]; (void)snprintf(required, sizeof(required), "Authorization: Bearer %s\r\n", token);
    if (!strstr(request, required) || !strstr(request, "ChatGPT-Account-Id: selected_fixture_workspace\r\n")) { respond(ssl, 401, "{}", 0); return; }
    unsigned status = mode == AUTH401 ? 401 : mode == AUTH403 ? 403 : mode == RATE429 ? 429 : mode == SERVER503 ? 503 : 200;
    if (status != 200) { respond(ssl, status, "{\"error\":\"synthetic\"}", 0); return; }
    if (mode == TRUNCATED) { respond(ssl, 200, "{\"items\":[", 0); return; }
    if (mode == DUPLICATE_KEY) { respond(ssl, 200, "{\"items\":[],\"total\":0,\"offset\":0,\"limit\":2,\"total\":0}", 0); return; }
    if (mode == OVERSIZED) { char *large = malloc(65537); if (!large) return; memset(large, 'x', 65536); large[65536] = 0; respond(ssl, 200, large, 0); free(large); return; }
    if (mode == SECRET_ECHO) { respond(ssl, 200, token, 1); return; }
    char path[8192], method[16];
    if (sscanf(request, "%15s %8191s", method, path) != 2 || strcmp(method, "GET")) { respond(ssl, 405, "{}", 0); return; }
    uint64_t offset; unsigned limit;
    if (sscanf(path, "/backend-api/conversations?offset=%" SCNu64 "&limit=%u&order=updated", &offset, &limit) == 2) {
        int archived = strstr(path, "is_archived=true") != NULL;
        const char *main_ids[] = { "conversation-1", "conversation-2" };
        const char *archive_ids[] = { "archived-1", "archived-2", "archived-3" };
        unsigned total = mode == EMPTY ? 0 : archived ? 3 : 2;
        if (!archived && !offset) ++*first_pages;
        if (!archived && mode == DUPLICATE) main_ids[1] = main_ids[0];
        if (!archived && mode == CONFLICT) main_ids[1] = main_ids[0];
        if (archived && mode == OVERLAP) archive_ids[0] = main_ids[0];
        unsigned declared = total;
        if (!archived && mode == TOTAL_DRIFT && offset) ++declared;
        if (!archived && mode == IMPOSSIBLE_TOTAL) declared = 1;
        if (!archived && mode == SHORT_TOTAL) declared = offset ? 2 : 1;
        uint64_t echoed = !archived && mode == REPEATED_OFFSET && offset ? 0 : offset;
        char body[4096]; int n = snprintf(body, sizeof(body), "{\"offset\":%" PRIu64 ",\"limit\":%u,\"total\":%u,\"items\":[", echoed, limit, declared);
        unsigned count = 0;
        while (offset + count < total && count < limit) {
            if (!archived && mode == PREMATURE_EMPTY) break;
            if (!archived && mode == SHORT_TOTAL && !offset && count == 1) break;
            const char *id = archived ? archive_ids[offset + count] : main_ids[offset + count];
            const char *title = (mode == CONFLICT && count) || (mode == INVENTORY_DRIFT && *first_pages > 1) ? "changed" : "stable";
            n += snprintf(body + n, sizeof(body) - (size_t)n, "%s{\"id\":\"%s\",\"title\":\"%s\"}", count ? "," : "", id, title);
            ++count;
        }
        (void)snprintf(body + n, sizeof(body) - (size_t)n, "]}"); respond(ssl, 200, body, 0); return;
    }
    const char prefix[] = "/backend-api/conversation/";
    if (!strncmp(path, prefix, sizeof(prefix) - 1)) {
        const char *id = path + sizeof(prefix) - 1;
        if (mode == STOP_SECOND && !strcmp(id, "conversation-2")) {
            if (event_fd < 0 || write(event_fd, "K", 1) != 1) return;
            char closed; ssize_t observed = read(event_fd, &closed, 1); (void)observed; return;
        }
        char body[8192]; detail(body, sizeof(body), id, mode);
        if (mode == MALFORMED_GRAPH) { char *message = strstr(body, "\"message\":null"); if (message) memcpy(message + 10, "true", 4); }
        respond(ssl, 200, body, 0); return;
    }
    respond(ssl, 404, "{}", 0);
}
static pid_t start_server(enum scenario mode, int event_fd)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0); int yes = 1;
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) return -1;
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_port = htons(18443); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 16)) { close(fd); return -1; }
    pid_t pid = fork();
    if (pid == 0) {
        (void)signal(SIGPIPE, SIG_IGN); unsigned first_pages = 0;
        for (;;) {
            int client = accept(fd, NULL, NULL); if (client < 0) _exit(1);
            SSL *ssl = SSL_new(server_context);
            if (ssl) { SSL_set_fd(ssl, client); if (SSL_accept(ssl) == 1) serve_request(ssl, mode, &first_pages, event_fd); SSL_free(ssl); }
            close(client);
        }
    }
    close(fd); return pid;
}
static void stop_server(pid_t pid)
{
    if (pid > 0) { (void)kill(pid, SIGTERM); (void)waitpid(pid, NULL, 0); }
}
static int certificate(void)
{
    EVP_PKEY_CTX *generator = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL); EVP_PKEY *key = NULL;
    if (!generator || EVP_PKEY_keygen_init(generator) <= 0 || EVP_PKEY_CTX_set_rsa_keygen_bits(generator, 2048) <= 0 || EVP_PKEY_keygen(generator, &key) <= 0) { EVP_PKEY_CTX_free(generator); return 0; }
    EVP_PKEY_CTX_free(generator); X509 *cert = X509_new();
    if (!cert) { EVP_PKEY_free(key); return 0; }
    (void)X509_set_version(cert, 2); (void)ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    (void)X509_gmtime_adj(X509_get_notBefore(cert), -60); (void)X509_gmtime_adj(X509_get_notAfter(cert), 3600);
    (void)X509_set_pubkey(cert, key); X509_NAME *name = X509_get_subject_name(cert);
    (void)X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"chatgpt.com", -1, -1, 0);
    (void)X509_set_issuer_name(cert, name);
    X509_EXTENSION *san = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, "DNS:chatgpt.com");
    if (san) { (void)X509_add_ext(cert, san, -1); X509_EXTENSION_free(san); }
    int ok = san != NULL && X509_sign(cert, key, EVP_sha256()) > 0;
    int fd = open("build/fixture-cert.pem", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    FILE *file = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!file || !PEM_write_X509(file, cert)) ok = 0;
    if (file && fclose(file)) ok = 0;
    server_context = SSL_CTX_new(TLS_server_method());
    if (!server_context || SSL_CTX_use_certificate(server_context, cert) != 1 || SSL_CTX_use_PrivateKey(server_context, key) != 1) ok = 0;
    X509_free(cert); EVP_PKEY_free(key); return ok;
}
static cgw_capture_bounds bounds(void)
{
    cgw_capture_bounds b = cgw_capture_default_bounds(); b.page_size = 2; b.max_response_bytes = 16384; b.max_conversations = 16; b.max_requests = 64; return b;
}
static int new_root(char root[128])
{
    memcpy(root, "build/capture-XXXXXX", sizeof("build/capture-XXXXXX")); return mkdtemp(root) != NULL;
}
static cgw_capture_report run(const char *root, enum scenario mode, cgw_capture_bounds b)
{
    pid_t server = start_server(mode, -1); check(server > 0, "synthetic TLS provider starts");
    cgw_capture_report report = {0};
    if (server > 0) (void)cgw_capture(root, workspace, authorize, NULL, b, &report);
    stop_server(server); return report;
}
static int find_name(const char *root, const char *prefix, char out[512])
{
    DIR *dir = opendir(root); if (!dir) return 0;
    struct dirent *entry; int found = 0;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, prefix, strlen(prefix))) {
        (void)snprintf(out, 512, "%s/%s", root, entry->d_name); found = 1; break;
    }
    closedir(dir); return found;
}
static void verify_raw_oracle(const char *root)
{
    char objects[512]; (void)snprintf(objects, sizeof(objects), "%s/objects", root);
    DIR *dir = opendir(objects); check(dir != NULL, "raw evidence exists on disk"); if (!dir) return;
    const char *expected_ids[] = { "conversation-1", "conversation-2", "archived-1", "archived-2", "archived-3" };
    unsigned found[5] = {0}; struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char path[1024], body[16385]; (void)snprintf(path, sizeof(path), "%s/%s", objects, entry->d_name);
        int fd = open(path, O_RDONLY | O_NOFOLLOW); ssize_t n = fd >= 0 ? read(fd, body, sizeof(body) - 1) : -1;
        if (fd >= 0) close(fd);
        check(n > 0, "raw object readable"); if (n <= 0) continue; body[n] = 0;
        check(strstr(body, token) == NULL, "no reflected token persisted");
        struct stat st; check(!lstat(path, &st) && (st.st_mode & 077) == 0 && S_ISREG(st.st_mode), "raw object private regular file");
        for (unsigned i = 0; i < 5; ++i) {
            char identity[300]; (void)snprintf(identity, sizeof(identity), "{\"id\":\"%s\",\"title\":\"Synthetic conversation\"", expected_ids[i]);
            if (!strncmp(body, identity, strlen(identity))) {
                ++found[i];
                check(strstr(body, "\"assistant-alt\":") && strstr(body, "\"future-role\"") && strstr(body, "\"future_unknown\""), "alternate branch and unknown provider fields preserved");
                check(strstr(body, "\"current_node\":\"assistant-1\"") != NULL, "selected branch retained independently of alternate");
            }
        }
    }
    closedir(dir);
    for (unsigned i = 0; i < 5; ++i) check(found[i] == 1, "independent expected semantic ID has exactly one complete raw graph");
    dir = opendir(root); if (!dir) return;
    while ((entry = readdir(dir))) check(strstr(entry->d_name, workspace) == NULL && strstr(entry->d_name, token) == NULL, "private identifiers absent from filenames");
    closedir(dir);
}
int main(int argc, char **argv)
{
    int mutant = argc == 2 && !strcmp(argv[1], "--mutant");
    if (argc > 1 && !mutant) { fprintf(stderr, "synthetic fixture entrypoint accepts no credentials, URLs or account arguments\n"); return 2; }
    (void)signal(SIGPIPE, SIG_IGN); (void)umask(077);
    (void)alarm(120);
    check(certificate(), "ephemeral certificate with fixed-origin hostname");
    if (failures) return 1;
    char root[128]; check(new_root(root), "private capture root created");
    cgw_capture_report report = run(root, GOOD, bounds());
#ifdef CGW_MUTANT_SILENT_OMISSION
    killed_expected_mutant = report.result == CGW_CAPTURE_COMPLETE && report.expected == 5 && report.completed == 4;
#endif
    check(report.result == CGW_CAPTURE_COMPLETE && report.expected == 5 && report.completed == 5, "end-to-end main and archived capture");
    check(!report.full_account_export_complete, "incomplete-account reporting");
    check(report.requests == 15, "archived pagination plus empty terminal probes and repeated inventory");
    verify_raw_oracle(root);
    report = run(root, GOOD, bounds());
    check(report.result == CGW_CAPTURE_COMPLETE && report.reused == 5 && report.requests == 10, "resume revalidates all markers and raw hashes");
    printf("capture_root\t%s\ninventory_sha256\t%s\nvalidation_sha256\t%s\n", root, report.inventory_sha256, report.report_sha256);
    if (!mutant) {
        FILE *location = fopen("build/capture-success.path", "w");
        check(location != NULL && fprintf(location, "%s\n", root) > 0, "retained genuine private capture location");
        if (location) check(!fclose(location), "capture location durable close");
    }
    char private_report[512]; check(find_name(root, "validation-", private_report), "on-disk validation report exists");
    check(new_root(root), "separate corrupt-object fixture root"); report = run(root, GOOD, bounds());
    char path[512];
    if (find_name(root, "complete-", path)) {
        int fd = open(path, O_RDONLY); char marker[256]; ssize_t n = fd >= 0 ? read(fd, marker, sizeof(marker) - 1) : -1; if (fd >= 0) close(fd);
        if (n > 0) {
            marker[n] = 0; char *hash = strrchr(marker, '\t');
            if (hash) { ++hash; hash[64] = 0; char object[512]; (void)snprintf(object, sizeof(object), "%s/objects/%s", root, hash);
                check(!chmod(object, 0600), "corrupt-hash fixture unlocks only its synthetic object");
                fd = open(object, O_WRONLY); check(fd >= 0 && write(fd, "X", 1) == 1, "corrupt-hash fixture changes actual object bytes"); if (fd >= 0) close(fd);
                check(!chmod(object, 0400), "corrupt-hash fixture restores immutable object mode");
                report = run(root, GOOD, bounds()); check(report.result != CGW_CAPTURE_COMPLETE, "corrupt object and invalid marker reuse refused");
#ifdef CGW_MUTANT_INVALID_MARKER
                killed_expected_mutant = report.result == CGW_CAPTURE_COMPLETE;
#endif
            } else check(0, "completion marker has a raw object hash");
        } else check(0, "completion marker readable");
    }
    const enum scenario rejected[] = { DUPLICATE, OVERLAP, CONFLICT, REPEATED_OFFSET, TOTAL_DRIFT, PREMATURE_EMPTY, IMPOSSIBLE_TOTAL,
        INVENTORY_DRIFT, TRUNCATED, GRAPH_CYCLE, MISSING_PARENT, DETAIL_PAGINATION, AUTH401, AUTH403, RATE429, SERVER503, OVERSIZED, SECRET_ECHO, SHORT_TOTAL, MALFORMED_GRAPH, DUPLICATE_KEY };
    const char *names[] = { "duplicate IDs", "overlapping pages/scopes", "conflicting IDs", "repeated offsets", "inconsistent totals", "premature empty page", "impossible totals",
        "drift across inventory passes", "truncated JSON", "graph cycle", "unresolved parent", "detail pagination uncertainty", "401", "403", "429", "5xx", "oversized response", "token echo across TLS records", "premature completion mutant", "malformed graph message", "duplicate JSON keys" };
    for (size_t i = 0; i < sizeof(rejected) / sizeof(*rejected); ++i) {
        check(new_root(root), "adversarial private root"); report = run(root, rejected[i], bounds());
        check(report.result != CGW_CAPTURE_COMPLETE, names[i]);
        if (rejected[i] == SECRET_ECHO) check(report.received_chunks >= 2, "reflected token actually crossed transport callback chunks");
#ifdef CGW_MUTANT_PREMATURE_COMPLETE
        if (rejected[i] == SHORT_TOTAL && report.result == CGW_CAPTURE_COMPLETE) killed_expected_mutant = 1;
#endif
        check(!report.full_account_export_complete, "adverse case never declares full account complete");
    }
    check(new_root(root), "empty root"); report = run(root, EMPTY, bounds());
    check(report.result == CGW_CAPTURE_COMPLETE && !report.expected && !report.completed && report.requests == 4, "recognized empty main and archived histories");
    cgw_capture_bounds small = bounds(); small.max_requests = 1;
    check(new_root(root), "request ceiling root"); report = run(root, GOOD, small); check(report.result != CGW_CAPTURE_COMPLETE, "work/request ceiling");
    check(new_root(root), "permission root"); (void)chmod(root, 0755);
    (void)cgw_capture(root, workspace, authorize, NULL, bounds(), &report); check(report.result == CGW_CAPTURE_REFUSED, "public-mode root refused");
    (void)chmod(root, 0700); char link[256]; (void)snprintf(link, sizeof(link), "%s-link", root); check(!symlink(root + 6, link), "synthetic symlink created");
    (void)cgw_capture(link, workspace, authorize, NULL, bounds(), &report); check(report.result == CGW_CAPTURE_REFUSED, "symlink root refused");
    if (!mutant) {
    check(new_root(root), "kill/restart root"); int events[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, events)) check(0, "kill/restart event channel");
    else {
        pid_t server = start_server(STOP_SECOND, events[1]);
        pid_t worker = fork();
        if (!worker) { cgw_capture_report child; (void)cgw_capture(root, workspace, authorize, NULL, bounds(), &child); _exit(1); }
        close(events[1]); char event; ssize_t n = read(events[0], &event, 1);
        check(n == 1 && event == 'K', "capture reached next conversation after durable first marker");
        if (worker > 0) { (void)kill(worker, SIGKILL); int status; (void)waitpid(worker, &status, 0); check(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "actual capture process killed"); }
        close(events[0]); stop_server(server);
        report = run(root, GOOD, bounds()); check(report.result == CGW_CAPTURE_COMPLETE && report.reused >= 1, "actual kill/restart preserves completed raw evidence");
    }
    if (find_name(root, "journal-", path)) {
        int fd = open(path, O_WRONLY | O_APPEND); check(fd >= 0 && write(fd, "TORN", 4) == 4, "interrupted-write fixture leaves an actual torn tail"); if (fd >= 0) close(fd);
        report = run(root, GOOD, bounds()); check(report.result != CGW_CAPTURE_COMPLETE, "interrupted journal tail retained incomplete");
    }
    }
    struct rusage usage; if (!getrusage(RUSAGE_SELF, &usage)) printf("fixture_process_peak_rss_kib\t%ld\n", usage.ru_maxrss);
    printf("assertions\t%u\nfailures\t%u\nevidence\thost-loopback-TLS-only\nfull_account_export_complete\tfalse\n", assertions, failures);
    SSL_CTX_free(server_context);
    if (mutant) { printf("targeted_mutant_rejected\t%s\n", killed_expected_mutant ? "true" : "false"); return failures && killed_expected_mutant ? 0 : 3; }
    return failures ? 1 : 0;
}
