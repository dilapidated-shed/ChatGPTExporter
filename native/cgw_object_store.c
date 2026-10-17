#define _GNU_SOURCE
#include "cgw_object_store.h"

#include <openssl/evp.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct cgw_store {
    int root_fd;
    int stage_dir_fd;
    int object_dir_fd;
    int stage_fd;
    char stage_name[48];
    EVP_MD_CTX *sha;
    uint64_t bytes;
    int active;
    int poisoned;
};

static int private_directory(int fd)
{
    struct stat info;
    return fd >= 0 && fstat(fd, &info) == 0
        && S_ISDIR(info.st_mode) && info.st_uid == geteuid()
        && (info.st_mode & 0077) == 0;
}

static int directory_at(int parent, const char *name)
{
    if (mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return -1;
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY
                   | O_NOFOLLOW | O_CLOEXEC);
    if (!private_directory(fd)) {
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

static int random_stage_name(char name[48])
{
    unsigned char entropy[16];
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    size_t used = 0;
    while (used < sizeof(entropy)) {
        ssize_t amount = read(fd, entropy + used, sizeof(entropy) - used);
        if (amount < 0 && errno == EINTR) continue;
        if (amount <= 0) { close(fd); return 0; }
        used += (size_t)amount;
    }
    close(fd);
    memcpy(name, "stage-", 6);
    static const char hexdigits[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(entropy); ++i) {
        name[6 + 2 * i] = hexdigits[entropy[i] >> 4];
        name[7 + 2 * i] = hexdigits[entropy[i] & 15];
    }
    name[6 + 2 * sizeof(entropy)] = '\0';
    return 1;
}

static void to_hex(const unsigned char *digest, size_t length, char *output)
{
    static const char hexdigits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        output[2 * i] = hexdigits[digest[i] >> 4];
        output[2 * i + 1] = hexdigits[digest[i] & 15];
    }
    output[2 * length] = '\0';
}

cgw_store *cgw_store_open(const char *private_root)
{
    if (private_root == NULL || private_root[0] == '\0') return NULL;
    cgw_store *store = (cgw_store *)calloc(1, sizeof(*store));
    if (store == NULL) return NULL;
    store->root_fd = -1;
    store->stage_dir_fd = -1;
    store->object_dir_fd = -1;
    store->stage_fd = -1;
    store->root_fd = open(private_root, O_RDONLY | O_DIRECTORY
                         | O_NOFOLLOW | O_CLOEXEC);
    if (!private_directory(store->root_fd)) goto fail;
    store->stage_dir_fd = directory_at(store->root_fd, ".staging");
    if (store->stage_dir_fd < 0) goto fail;
    store->object_dir_fd = directory_at(store->root_fd, "objects");
    if (store->object_dir_fd < 0) goto fail;
    /* Persist newly created private child directory entries. */
    if (fsync(store->root_fd) != 0) goto fail;
    store->sha = EVP_MD_CTX_new();
    if (store->sha == NULL) goto fail;
    return store;
fail:
    cgw_store_close(store);
    return NULL;
}

void cgw_store_abort(cgw_store *store)
{
    if (store == NULL) return;
    if (store->stage_fd >= 0) {
        close(store->stage_fd);
        store->stage_fd = -1;
    }
    if (store->stage_name[0] != '\0' && store->stage_dir_fd >= 0)
        (void)unlinkat(store->stage_dir_fd, store->stage_name, 0);
    store->stage_name[0] = '\0';
    store->active = 0;
    store->poisoned = 0;
    store->bytes = 0;
}

void cgw_store_close(cgw_store *store)
{
    if (store == NULL) return;
    cgw_store_abort(store);
    EVP_MD_CTX_free(store->sha);
    if (store->object_dir_fd >= 0) close(store->object_dir_fd);
    if (store->stage_dir_fd >= 0) close(store->stage_dir_fd);
    if (store->root_fd >= 0) close(store->root_fd);
    free(store);
}

cgw_store_result cgw_store_begin(cgw_store *store)
{
    if (store == NULL || store->active) return CGW_STORE_INVALID_STATE;
    if (EVP_DigestInit_ex(store->sha, EVP_sha256(), NULL) != 1)
        return CGW_STORE_IO_ERROR;
    store->bytes = 0;
    store->poisoned = 0;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (!random_stage_name(store->stage_name)) break;
        store->stage_fd = openat(store->stage_dir_fd, store->stage_name,
                                 O_WRONLY | O_CREAT | O_EXCL
                                 | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (store->stage_fd >= 0) {
            store->active = 1;
            return CGW_STORE_OK;
        }
        if (errno != EEXIST) break;
    }
    store->stage_name[0] = '\0';
    return CGW_STORE_IO_ERROR;
}

cgw_store_result cgw_store_write(cgw_store *store,
                                  const unsigned char *bytes, size_t length)
{
    if (store == NULL || !store->active || store->poisoned
        || (bytes == NULL && length != 0)) return CGW_STORE_INVALID_STATE;
    if (length > UINT64_MAX - store->bytes) {
        store->poisoned = 1;
        return CGW_STORE_IO_ERROR;
    }
    size_t used = 0;
    while (used < length) {
        ssize_t written = write(store->stage_fd, bytes + used, length - used);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) {
            store->poisoned = 1;
            return CGW_STORE_IO_ERROR;
        }
        if (EVP_DigestUpdate(store->sha, bytes + used, (size_t)written) != 1) {
            store->poisoned = 1;
            return CGW_STORE_IO_ERROR;
        }
        used += (size_t)written;
    }
    store->bytes += (uint64_t)used;
    return CGW_STORE_OK;
}

int cgw_store_sink(void *context, const unsigned char *bytes, size_t length)
{
    return cgw_store_write((cgw_store *)context, bytes, length) == CGW_STORE_OK;
}

static cgw_store_result existing_object(int directory, const char *name,
                                        uint64_t expected_bytes)
{
    int fd = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return CGW_STORE_IO_ERROR;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid()
        || (st.st_mode & 0077) != 0 || st.st_size < 0
        || (uint64_t)st.st_size != expected_bytes) {
        close(fd);
        return CGW_STORE_EXISTING_OBJECT_CORRUPT;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (ctx == NULL) { close(fd); return CGW_STORE_IO_ERROR; }
    cgw_store_result result = CGW_STORE_OK;
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1)
        result = CGW_STORE_IO_ERROR;
    unsigned char bytes[8192];
    while (result == CGW_STORE_OK) {
        ssize_t count = read(fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { result = CGW_STORE_IO_ERROR; break; }
        if (count == 0) break;
        if (EVP_DigestUpdate(ctx, bytes, (size_t)count) != 1)
            result = CGW_STORE_IO_ERROR;
    }
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned amount = 0;
    if (result == CGW_STORE_OK
        && (EVP_DigestFinal_ex(ctx, digest, &amount) != 1 || amount != 32)) {
        result = CGW_STORE_IO_ERROR;
    }
    char verified_name[65] = {0};
    if (result == CGW_STORE_OK) {
        to_hex(digest, amount, verified_name);
        if (strcmp(verified_name, name) != 0)
            result = CGW_STORE_EXISTING_OBJECT_CORRUPT;
    }
    EVP_MD_CTX_free(ctx);
    close(fd);
    return result;
}

cgw_store_result cgw_store_commit(cgw_store *store, cgw_store_receipt *receipt)
{
    if (receipt != NULL) memset(receipt, 0, sizeof(*receipt));
    if (store == NULL || !store->active || store->poisoned
        || receipt == NULL) return CGW_STORE_INVALID_STATE;

    if (fchmod(store->stage_fd, 0400) != 0 || fsync(store->stage_fd) != 0)
        return CGW_STORE_IO_ERROR;

    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned length = 0;
    if (EVP_DigestFinal_ex(store->sha, digest, &length) != 1 || length != 32)
        return CGW_STORE_IO_ERROR;
    char hash[65];
    to_hex(digest, length, hash);

    int already_present = 0;
    if (linkat(store->stage_dir_fd, store->stage_name,
               store->object_dir_fd, hash, 0) != 0) {
        if (errno != EEXIST) return CGW_STORE_IO_ERROR;
        cgw_store_result existing =
            existing_object(store->object_dir_fd, hash, store->bytes);
        if (existing != CGW_STORE_OK) return existing;
        already_present = 1;
    }
    if (fsync(store->object_dir_fd) != 0) return CGW_STORE_IO_ERROR;
    if (unlinkat(store->stage_dir_fd, store->stage_name, 0) != 0
        || fsync(store->stage_dir_fd) != 0) return CGW_STORE_IO_ERROR;

    /* No archive-complete marker; only the immutable raw object is durable. */
    memcpy(receipt->sha256, hash, sizeof(hash));
    receipt->bytes = store->bytes;
    receipt->already_present = already_present;
    store->stage_name[0] = '\0';
    if (close(store->stage_fd) != 0) {
        store->stage_fd = -1;
        memset(receipt, 0, sizeof(*receipt));
        return CGW_STORE_IO_ERROR;
    }
    store->stage_fd = -1;
    store->active = 0;
    store->bytes = 0;
    return CGW_STORE_OK;
}

const char *cgw_store_result_name(cgw_store_result result)
{
    switch (result) {
    case CGW_STORE_OK: return "ok";
    case CGW_STORE_INVALID_ROOT: return "invalid_root";
    case CGW_STORE_PRIVATE_DIRECTORY_REQUIRED: return "private_directory_required";
    case CGW_STORE_IO_ERROR: return "io_error";
    case CGW_STORE_INVALID_STATE: return "invalid_state";
    case CGW_STORE_EXISTING_OBJECT_CORRUPT: return "existing_object_corrupt";
    }
    return "unknown_store_result";
}
