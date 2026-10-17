/* Crash-aware, append-preserving native raw object store.
 *
 * Caller selects a private local directory. No credentials, provider
 * serialization, normalized Markdown, network, or archive-complete marker.
 */
#ifndef CGW_OBJECT_STORE_H
#define CGW_OBJECT_STORE_H
#include <stddef.h>
#include <stdint.h>

typedef struct cgw_store cgw_store;

typedef enum {
    CGW_STORE_OK = 0,
    CGW_STORE_INVALID_ROOT,
    CGW_STORE_PRIVATE_DIRECTORY_REQUIRED,
    CGW_STORE_IO_ERROR,
    CGW_STORE_INVALID_STATE,
    CGW_STORE_EXISTING_OBJECT_CORRUPT
} cgw_store_result;

typedef struct {
    char sha256[65]; /* lowercase hex; NUL terminated */
    uint64_t bytes;
    int already_present; /* verified byte-for-byte digest, not merely filename */
} cgw_store_receipt;

/* Root must already exist, be owned by the calling user and be mode 0700
 * or stricter. Final root component cannot be a symbolic link.
 * Creates private .staging/ and objects/ children if missing. */
cgw_store *cgw_store_open(const char *private_root);
void cgw_store_close(cgw_store *store);

/* Exactly one uncommitted object may be staged per store instance.
 * A failed begin/write/commit requires abort before another begin. */
cgw_store_result cgw_store_begin(cgw_store *store);
cgw_store_result cgw_store_write(cgw_store *store,
                                  const unsigned char *bytes, size_t length);

/* Adapter for cgw_fetcher_execute: 1 accepted, 0 rejected. */
int cgw_store_sink(void *context, const unsigned char *bytes, size_t length);

/* Publishes under objects/<sha256> without ever overwriting an existing
 * object. fsyncs the stage and object directory; no completion marker is
 * produced. Caller may only invoke this after a successful transport result.
 * On any failure call cgw_store_abort(). */
cgw_store_result cgw_store_commit(cgw_store *store,
                                   cgw_store_receipt *receipt);

/* Removes the currently staged temporary object. Unrelated completed
 * objects and orphan stages from previous processes are never deleted. */
void cgw_store_abort(cgw_store *store);

const char *cgw_store_result_name(cgw_store_result result);

#endif
