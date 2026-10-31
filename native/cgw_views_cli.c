#define _POSIX_C_SOURCE 200809L
#include "cgw_views.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned char *read_source(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb"); if (!file) return NULL;
    size_t cap = 4194304; unsigned char *bytes = malloc(cap + 1);
    if (!bytes) { fclose(file); return NULL; }
    *length = fread(bytes, 1, cap + 1, file);
    int ok = !ferror(file) && *length <= cap; fclose(file);
    if (!ok) { free(bytes); return NULL; } return bytes;
}
static int publish(const char *path, const char *bytes, size_t length)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600); if (fd < 0) return 0;
    int ok = 1;
    while (length) {
        ssize_t n = write(fd, bytes, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = 0; break; } bytes += n; length -= (size_t)n;
    }
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    return ok;
}
int main(int argc, char **argv)
{
    if (argc != 7 || (strcmp(argv[4], "main") && strcmp(argv[4], "archived"))) {
        fputs("usage: cgw-views DETAIL_JSON LISTING_JSON WORKSPACE_ID main|archived NEW_JSON NEW_MARKDOWN\n", stderr); return 2;
    }
    size_t a = 0, b = 0; unsigned char *detail = read_source(argv[1], &a), *listing = read_source(argv[2], &b);
    cgw_views views = {0};
    int ok = detail && listing && cgw_views_normalize(detail, a, listing, b, argv[3], !strcmp(argv[4], "archived"), 16000000, &views);
    if (ok) ok = publish(argv[5], views.json, views.json_bytes) && publish(argv[6], views.markdown, views.markdown_bytes);
    cgw_views_free(&views); free(detail); free(listing); return ok ? 0 : 1;
}
