#define _POSIX_C_SOURCE 200809L
#include "cgw_capture.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) { fprintf(stderr, "Usage: cgw-verify PRIVATE_ARCHIVE [WORKSPACE_ID]\n"); return 2; }
    char workspace[258] = {0};
    if (argc == 3) {
        if (strlen(argv[2]) > 256) return 2;
        memcpy(workspace, argv[2], strlen(argv[2]));
    } else {
        int root = open(argv[1], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int fd = root >= 0 ? openat(root, "workspace.txt", O_RDONLY | O_NOFOLLOW | O_CLOEXEC) : -1;
        struct stat state; ssize_t length = -1;
        if (fd >= 0 && !fstat(fd, &state) && S_ISREG(state.st_mode) && state.st_uid == geteuid() && !(state.st_mode & 077)) length = read(fd, workspace, 257);
        if (fd >= 0) close(fd);
        if (root >= 0) close(root);
        if (length <= 0 || length > 256) { fprintf(stderr, "Private workspace provenance unavailable.\n"); return 2; }
        workspace[length] = 0;
    }
    cgw_capture_bounds bounds = cgw_capture_default_bounds();
    bounds.page_size = 100; bounds.max_conversations = 8192; bounds.max_requests = 16384;
    bounds.max_graph_nodes = 2048; bounds.max_response_bytes = 4194304;
    bounds.max_json_work = 16000000; bounds.max_received_bytes = UINT64_C(8589934592);
    cgw_capture_report report;
    cgw_capture_result result = cgw_capture_revalidate(argv[1], workspace, bounds, &report);
    printf("disk_validation\t%s\nexpected\t%u\nverified\t%u\nfull_account_export_complete\tfalse\nreason\t%s\n",
           result == CGW_CAPTURE_COMPLETE ? "PASS_BOUNDED_MAIN_ARCHIVED" : "INCONCLUSIVE", report.expected, report.completed, report.reason);
    return result == CGW_CAPTURE_COMPLETE ? 0 : 1;
}
