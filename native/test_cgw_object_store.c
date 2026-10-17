#define _GNU_SOURCE
#include "cgw_object_store.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned assertions;
static void check(int ok, const char *why)
{
    ++assertions;
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); exit(1); }
}

static void path(char *dest, size_t cap, const char *root, const char *relative)
{
    check(snprintf(dest, cap, "%s/%s", root, relative) < (int)cap,
          "test filesystem path capacity");
}

int main(void)
{
    char root[] = "/tmp/cgw-objects-XXXXXX";
    check(mkdtemp(root) != NULL, "private test root created");

    cgw_store *store = cgw_store_open(root);
    check(store != NULL, "private store opens");
    check(cgw_store_begin(store) == CGW_STORE_OK, "first stage");
    check(cgw_store_write(store, (const unsigned char *)"a", 1) == CGW_STORE_OK,
          "first chunk");
    check(cgw_store_sink(store, (const unsigned char *)"bc", 2) == 1,
          "stream adapter chunk");
    cgw_store_receipt receipt;
    check(cgw_store_commit(store, &receipt) == CGW_STORE_OK, "commit fsynced");
    check(strcmp(receipt.sha256,
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
          == 0, "canonical SHA-256");
    check(receipt.bytes == 3 && receipt.already_present == 0,
          "first object is new and exactly three bytes");

    char final[512];
    path(final, sizeof(final), root, "objects/"
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    int fd = open(final, O_RDONLY | O_NOFOLLOW);
    check(fd >= 0, "published hashed object exists");
    char bytes[4] = {0};
    check(read(fd, bytes, 3) == 3 && strcmp(bytes, "abc") == 0,
          "durable content matches original");
    struct stat st;
    check(fstat(fd, &st) == 0 && (st.st_mode & 0777) == 0400,
          "object not group/world-readable");
    close(fd);

    check(cgw_store_begin(store) == CGW_STORE_OK, "second stage");
    check(cgw_store_sink(store, (const unsigned char *)"abc", 3),
          "duplicate content staged");
    memset(&receipt, 0, sizeof(receipt));
    check(cgw_store_commit(store, &receipt) == CGW_STORE_OK,
          "identical immutable object deduplicated");
    check(receipt.already_present == 1, "duplicate validated rather than overwritten");

    check(cgw_store_begin(store) == CGW_STORE_OK, "partial stage");
    check(cgw_store_write(store, (const unsigned char *)"partial", 7)
          == CGW_STORE_OK, "partial bytes staged");
    cgw_store_abort(store);
    check(cgw_store_commit(store, &receipt) == CGW_STORE_INVALID_STATE,
          "aborted bytes cannot be completed");
    check(receipt.bytes == 0 && receipt.sha256[0] == '\0',
          "failed completion publishes no receipt");

    check(chmod(final, 0600) == 0, "enable deliberate private corruption");
    fd = open(final, O_WRONLY | O_TRUNC);
    check(fd >= 0 && write(fd, "bad", 3) == 3 && fsync(fd) == 0,
          "corruption fixture written");
    close(fd);
    check(chmod(final, 0400) == 0, "restore protected file permissions");
    check(cgw_store_begin(store) == CGW_STORE_OK, "third stage");
    check(cgw_store_sink(store, (const unsigned char *)"abc", 3),
          "expected original content restaged");
    check(cgw_store_commit(store, &receipt) == CGW_STORE_EXISTING_OBJECT_CORRUPT,
          "matching filename without matching contents rejected");
    check(receipt.bytes == 0, "corruption does not publish receipt");
    cgw_store_abort(store);

    cgw_store_close(store);

    check(chmod(root, 0755) == 0, "fixture makes root readable");
    check(cgw_store_open(root) == NULL, "reject group/world-readable root");
    check(chmod(root, 0700) == 0, "restore root permissions");
    char alias[512];
    check(snprintf(alias, sizeof(alias), "%s.link", root) < (int)sizeof(alias),
          "alias path bound");
    check(symlink(root, alias) == 0, "create symbolic-link test root");
    check(cgw_store_open(alias) == NULL, "reject symlink destination");
    unlink(alias);

    char other[] = "/tmp/cgw-link-dir-XXXXXX";
    check(mkdtemp(other) != NULL, "second private root");
    char injected[512];
    path(injected, sizeof(injected), other, ".staging");
    check(symlink(root, injected) == 0, "create injected child link");
    check(cgw_store_open(other) == NULL, "reject symlink staging child");
    unlink(injected);
    rmdir(other);

    check(unlink(final) == 0, "remove synthetic object");
    path(injected, sizeof(injected), root, "objects");
    check(rmdir(injected) == 0, "remove synthetic object dir");
    path(injected, sizeof(injected), root, ".staging");
    check(rmdir(injected) == 0, "no stale stage files after abort/commit");
    check(rmdir(root) == 0, "remove synthetic private root");

    printf("PASS %u native private object-store assertions\n", assertions);
    return 0;
}
