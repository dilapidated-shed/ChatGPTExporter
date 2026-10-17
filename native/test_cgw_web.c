#include "cgw_web.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned assertions;

static void check(int condition, const char *message)
{
    ++assertions;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expected(cgw_input input, cgw_method method, const char *path,
                     const char *body, size_t max_bytes, int auth)
{
    cgw_http_request out;
    check(cgw_resolve(&input, &out) == CGW_OK, "request accepted");
    check(out.method == method, "expected HTTP method");
    check(strcmp(out.path, path) == 0, "exact allowlisted path");
    check(strcmp(out.body, body) == 0, "exact bounded body");
    check(out.response_limit_bytes == max_bytes, "expected byte limit");
    check(out.requires_authentication == auth, "authentication classification");
    check(out.path[0] == '/' && out.path[1] != '/', "relative same-origin path");
}

static void rejected(cgw_input input, cgw_status status)
{
    cgw_http_request out;
    memset(&out, 0x55, sizeof(out));
    check(cgw_resolve(&input, &out) == status, "malformed request rejected");
    check(out.path[0] == '\0' && out.body[0] == '\0',
          "rejection publishes no partial request");
    check(out.method == 0 && out.response_limit_bytes == 0,
          "rejection clears request authority");
}

static void static_operations(void)
{
    cgw_input x = {0};
    x.operation = CGW_SESSION_PROBE;
    expected(x, CGW_GET, "/api/auth/session", "", 1000000, 0);
    x.operation = CGW_ACCOUNTS_LIST;
    expected(x, CGW_GET, "/backend-api/accounts/check/v4-2023-04-27",
             "", 5000000, 1);
    x.operation = CGW_ACCOUNT_ARTIFACT;
    x.arguments.artifact.kind = CGW_MEMORIES;
    expected(x, CGW_GET, "/backend-api/memories?include_memory_entries=true",
             "", 20000000, 1);
    x.arguments.artifact.kind = CGW_CUSTOM_INSTRUCTIONS;
    expected(x, CGW_GET, "/backend-api/user_system_messages",
             "", 20000000, 1);
    x.arguments.artifact.kind = CGW_SETTINGS;
    expected(x, CGW_GET, "/backend-api/settings", "", 20000000, 1);
    x.arguments.artifact.kind = CGW_BETA_FEATURES;
    expected(x, CGW_GET, "/backend-api/settings/beta_features",
             "", 20000000, 1);
    x.arguments.artifact.kind = (cgw_artifact_kind)99;
    rejected(x, CGW_INVALID_PARAMETERS);
}

static void pages(void)
{
    cgw_input x = {0};
    x.operation = CGW_CONVERSATION_PAGE;
    x.arguments.conversation_page.offset = 20;
    x.arguments.conversation_page.limit = 100;
    expected(x, CGW_GET, "/backend-api/conversations?offset=20&limit=100&order=updated",
             "", 20000000, 1);
    x.arguments.conversation_page.archived = 1;
    expected(x, CGW_GET, "/backend-api/conversations?offset=20&limit=100"
             "&order=updated&is_archived=true", "", 20000000, 1);
    x.arguments.conversation_page.archived = 2;
    rejected(x, CGW_INVALID_PARAMETERS);
    x.arguments.conversation_page.archived = 0;
    x.arguments.conversation_page.limit = 101;
    rejected(x, CGW_INVALID_PARAMETERS);
    x.arguments.conversation_page.limit = 100;
    x.arguments.conversation_page.offset = UINT64_C(9007199254740992);
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_SHARED_PAGE;
    x.arguments.shared_page.offset = 1;
    x.arguments.shared_page.limit = 5;
    expected(x, CGW_GET, "/backend-api/shared_conversations?order=updated&limit=5&offset=1",
             "", 20000000, 1);
    x.arguments.shared_page.limit = 0;
    rejected(x, CGW_INVALID_PARAMETERS);
}

static void cursors_and_details(void)
{
    cgw_input x = {0};
    x.operation = CGW_PROJECT_PAGE;
    x.arguments.project_page.cursor = NULL;
    expected(x, CGW_GET,
             "/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0",
             "", 20000000, 1);
    x.arguments.project_page.cursor = "a&other=evil/#";
    expected(x, CGW_GET,
             "/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0"
             "&cursor=a%26other%3Devil%2F%23", "", 20000000, 1);
    x.arguments.project_page.cursor = "bad\nheader";
    rejected(x, CGW_INVALID_PARAMETERS);
    x.arguments.project_page.cursor = "";
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_PROJECT_CONVERSATION_PAGE;
    x.arguments.project_conversation_page.project_id = "project_3";
    x.arguments.project_conversation_page.cursor = "x/y";
    expected(x, CGW_GET, "/backend-api/gizmos/project_3/conversations?cursor=x%2Fy",
             "", 20000000, 1);
    x.arguments.project_conversation_page.project_id = "../admin";
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_SHARED_DETAIL;
    x.arguments.detail.id = "share-99";
    expected(x, CGW_GET, "/backend-api/share/share-99", "", 100000000, 1);
    x.operation = CGW_CONVERSATION_DETAIL;
    x.arguments.detail.id = "conversation_99";
    expected(x, CGW_GET, "/backend-api/conversation/conversation_99",
             "", 100000000, 1);
    x.arguments.detail.id = "bad?next=https://example.org";
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_CONVERSATION_CURRENT;
    x.arguments.current.conversation_id = "conv";
    x.arguments.current.turns = 100;
    expected(x, CGW_GET,
             "/backend-api/conversations/conv?include_has_versions=true&num_turns=100",
             "", 100000000, 1);
    x.arguments.current.turns = 0;
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_CONVERSATION_MESSAGES;
    x.arguments.messages.conversation_id = "conv";
    x.arguments.messages.before = "anchor+1";
    x.arguments.messages.turns = 10;
    expected(x, CGW_GET, "/backend-api/conversations/conv/messages?before=anchor%2B1"
             "&include_has_versions=true&num_turns=10", "", 100000000, 1);
    x.arguments.messages.before = "\r";
    rejected(x, CGW_INVALID_PARAMETERS);
}

static void batch_and_asset(void)
{
    const char *two[] = { "one", "two" };
    cgw_input x = {0};
    x.operation = CGW_CONVERSATION_BATCH;
    x.arguments.batch.conversation_ids = two;
    x.arguments.batch.count = 2;
    expected(x, CGW_POST, "/backend-api/conversations/batch",
             "{\"conversation_ids\":[\"one\",\"two\"]}", 100000000, 1);
    x.arguments.batch.count = 0;
    rejected(x, CGW_INVALID_PARAMETERS);
    x.arguments.batch.count = 11;
    rejected(x, CGW_INVALID_PARAMETERS);
    const char *duplicates[] = { "same", "same" };
    x.arguments.batch.conversation_ids = duplicates;
    x.arguments.batch.count = 2;
    rejected(x, CGW_INVALID_PARAMETERS);
    const char *bad[] = { "one", "bad\"}" };
    x.arguments.batch.conversation_ids = bad;
    rejected(x, CGW_INVALID_PARAMETERS);

    x.operation = CGW_ASSET_OPEN;
    x.arguments.asset.file_id = "file-1";
    x.arguments.asset.parent_id = "conv_2";
    x.arguments.asset.scope = CGW_ASSET_CONVERSATION;
    expected(x, CGW_GET,
             "/backend-api/files/download/file-1?conversation_id=conv_2&inline=false",
             "", 5000000, 1);
    x.arguments.asset.scope = CGW_ASSET_PROJECT;
    expected(x, CGW_GET,
             "/backend-api/files/download/file-1?gizmo_id=conv_2",
             "", 5000000, 1);
    x.arguments.asset.scope = (cgw_asset_scope)0;
    rejected(x, CGW_INVALID_PARAMETERS);
    x.arguments.asset.scope = CGW_ASSET_CONVERSATION;
    x.arguments.asset.parent_id = "x&evil=1";
    rejected(x, CGW_INVALID_PARAMETERS);
}

int main(void)
{
    static_operations();
    pages();
    cursors_and_details();
    batch_and_asset();
    cgw_input invalid = {0};
    rejected(invalid, CGW_INVALID_OPERATION);
    check(cgw_resolve(NULL, NULL) == CGW_INVALID_PARAMETERS, "null arguments rejected");
    check(strcmp(cgw_status_name(CGW_OK), "ok") == 0, "status name");
    printf("PASS %u native web-contract assertions (no network or credentials)\n",
           assertions);
    return 0;
}
