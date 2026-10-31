# Native private raw object store

The native HTTPS adapter in PR #11 exposes only a **streaming sink**. This
additional store provides a reversible filesystem sink for archive raw bytes,
without interpreting their ChatGPT graph content.

\`cgw_object_store.[ch]\` uses ordinary POSIX file descriptors, SHA-256 via
OpenSSL EVP, and fixed 8 KiB verification buffers. It is intended to work on
Linux and Android; this draft has only host acceptance.

1. The user must select an already existing **private** directory. The store
   rejects directories not owned by the current process user or readable/
   writable by group/others (0700 or stricter). It refuses a symlink at the
   final root component and any symlink in its two managed child directories.
2. It creates \`.staging/\` and \`objects/\` under that root, mode 0700.
   Stage filenames use 128 bits from the kernel random device, with O_EXCL,
   O_NOFOLLOW and mode 0600. The stage stays uncommitted during network I/O.
3. \`cgw_store_sink\` streams bytes into a regular file and an incremental
   digest. The caller invokes \`cgw_store_commit\` **only after a complete
   successful HTTPS response**. It changes the stage to owner-read-only,
   fsyncs its contents, and uses an atomic hard link to publish as
   \`objects/<sha256>\`. Existing objects must be verified against their
   own full digest and expected size before deduplication.
4. It fsyncs the object directory, removes the stage file, and fsyncs the
   staging directory before producing a success receipt with hash, bytes
   and deduplication flag. No existing object is replaced. On HTTP failure,
   connection uncertainty, sink failure, process interruption or hash conflict,
   do **not** publish a completed conversation: abort the pending stage and
   report the exact condition.
5. Orphan staging files after a process crash are not silently deleted. This
   implementation has no authenticated request journal, run index, project
   folder, attachment continuation, normalization, audit receipt, migration,
   deletion, or remote communication. Its success receipt certifies one raw
   object, **never an account-wide export**.

## Synthetic tests

    make -C native check-store
    make -C native check-https

The first verifies content hashes, private modes, staging, duplicate detection,
the non-overwrite rule, corrupt existing data, abort and symlink rejection.
The second streams actual synthetic HTTPS fixture data into the same store,
checks the published object hash and bytes, and tests rollback after HTTP 429.
No ChatGPT account or browser is contacted.

Host success is **not** evidence of fsync guarantees on a particular Android
flash device, F2FS/ext4 mount or SD card. Crash/power-loss tests, exact-source
ICK builds and physical TAB_P10 acceptance remain separate. A1/C67 are
historical targets and are unavailable. The Chromium extension's
existing archival validation remains authoritative until this native
implementation grows a complete independently audited archive.

The original candidate used a Flexible Pipes work envelope. The completion
integration executes directly; another orchestration registration is not a
prerequisite. Independent source/runtime evidence is still required.

## Android atomic publication

The installed Android 14 capture at source
`560f13798b242999199b20e3340f5150cecbadbf` refused its first object with
`object_commit_link_object_errno_13`. The
[Android 14 policy](https://android.googlesource.com/platform/system/sepolicy/+/refs/tags/android-security-14.0.0_r1/private/app_neverallows.te)
forbids app hard links even within app-private storage.

Android now publishes the fsynced private stage with
`renameat2(RENAME_NOREPLACE)`. An existing destination is rehashed and reused
only if its contents match; no path falls back to overwriting rename or copy.
The object and staging directory entries are fsynced before a receipt is
returned. An unsupported operation is an explicit failure. The existing
non-Android link-based implementation remains qualified separately.

`make -C native check-store-android` exercises this exact syscall branch on
the Linux build host, including inode preservation, corrupted prior evidence,
complete capture, actual process termination/restart and torn-journal recovery.
Installed Android and physical tablet acceptance remain separate evidence.
