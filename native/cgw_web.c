#include "cgw_web.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define CGW_MAX_SAFE_OFFSET UINT64_C(9007199254740991)

static int appendf(char *dest, size_t capacity, size_t *used, const char *format, ...)
{
    if (*used >= capacity) return 0;
    va_list args;
    va_start(args, format);
    int count = vsnprintf(dest + *used, capacity - *used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= capacity - *used) return 0;
    *used += (size_t)count;
    return 1;
}

static int identifier(const char *value)
{
    if (value == NULL) return 0;
    size_t length = 0;
    while (value[length] != '\0') {
        unsigned char byte = (unsigned char)value[length];
        int accepted = (byte >= 'A' && byte <= 'Z')
            || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9')
            || byte == '_' || byte == '-';
        if (!accepted || ++length > 256) return 0;
    }
    return length != 0;
}

static int cursor(const char *value)
{
    if (value == NULL) return 0;
    size_t length = 0;
    while (value[length] != '\0') {
        unsigned char byte = (unsigned char)value[length];
        if (byte < 32 || byte == 127 || ++length > 2048) return 0;
    }
    return length != 0;
}

static int encoded_cursor(char *dest, size_t capacity, size_t *used, const char *value)
{
    if (!cursor(value)) return 0;
    for (size_t i = 0; value[i] != '\0'; ++i) {
        unsigned char byte = (unsigned char)value[i];
        int unreserved = (byte >= 'A' && byte <= 'Z')
            || (byte >= 'a' && byte <= 'z')
            || (byte >= '0' && byte <= '9')
            || byte == '-' || byte == '.' || byte == '_' || byte == '~';
        if (unreserved) {
            if (!appendf(dest, capacity, used, "%c", (int)byte)) return 0;
        } else {
            if (!appendf(dest, capacity, used, "%%%02X", (unsigned)byte)) return 0;
        }
    }
    return 1;
}

static int page(uint64_t offset, unsigned limit)
{
    return offset <= CGW_MAX_SAFE_OFFSET && limit >= 1 && limit <= 100;
}

cgw_status cgw_resolve(const cgw_input *input, cgw_http_request *output)
{
    if (output == NULL) return CGW_INVALID_PARAMETERS;
    memset(output, 0, sizeof(*output));
    if (input == NULL) return CGW_INVALID_PARAMETERS;

    /* Construct privately; publish only on full validation. */
    cgw_http_request request = {0};
    request.method = CGW_GET;
    request.requires_authentication = 1;
    size_t path_used = 0, body_used = 0;
    int written = 0;
    const char *value;

    switch (input->operation) {
    case CGW_SESSION_PROBE:
        request.requires_authentication = 0;
        request.response_limit_bytes = 1000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/api/auth/session");
        break;
    case CGW_ACCOUNTS_LIST:
        request.response_limit_bytes = 5000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/accounts/check/v4-2023-04-27");
        break;
    case CGW_CONVERSATION_PAGE:
        if (!page(input->arguments.conversation_page.offset,
                  input->arguments.conversation_page.limit)
            || (input->arguments.conversation_page.archived != 0
                && input->arguments.conversation_page.archived != 1))
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 20000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/conversations?offset=%" PRIu64
                          "&limit=%u&order=updated%s",
                          input->arguments.conversation_page.offset,
                          input->arguments.conversation_page.limit,
                          input->arguments.conversation_page.archived
                          ? "&is_archived=true" : "");
        break;
    case CGW_PROJECT_PAGE:
        request.response_limit_bytes = 20000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/gizmos/snorlax/sidebar?conversations_per_gizmo=0");
        if (written && input->arguments.project_page.cursor != NULL) {
            if (!cursor(input->arguments.project_page.cursor))
                return CGW_INVALID_PARAMETERS;
            written = appendf(request.path, sizeof(request.path), &path_used,
                              "&cursor=")
                && encoded_cursor(request.path, sizeof(request.path),
                                  &path_used, input->arguments.project_page.cursor);
        }
        break;
    case CGW_PROJECT_CONVERSATION_PAGE:
        if (!identifier(input->arguments.project_conversation_page.project_id)
            || !cursor(input->arguments.project_conversation_page.cursor))
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 20000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/gizmos/%s/conversations?cursor=",
                          input->arguments.project_conversation_page.project_id)
            && encoded_cursor(request.path, sizeof(request.path), &path_used,
                              input->arguments.project_conversation_page.cursor);
        break;
    case CGW_SHARED_PAGE:
        if (!page(input->arguments.shared_page.offset,
                  input->arguments.shared_page.limit))
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 20000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/shared_conversations?order=updated"
                          "&limit=%u&offset=%" PRIu64,
                          input->arguments.shared_page.limit,
                          input->arguments.shared_page.offset);
        break;
    case CGW_SHARED_DETAIL:
    case CGW_CONVERSATION_DETAIL:
        if (!identifier(input->arguments.detail.id)) return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 100000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          input->operation == CGW_SHARED_DETAIL
                          ? "/backend-api/share/%s" : "/backend-api/conversation/%s",
                          input->arguments.detail.id);
        break;
    case CGW_CONVERSATION_CURRENT:
        if (!identifier(input->arguments.current.conversation_id)
            || input->arguments.current.turns < 1
            || input->arguments.current.turns > 100)
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 100000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/conversations/%s?include_has_versions=true&num_turns=%u",
                          input->arguments.current.conversation_id,
                          input->arguments.current.turns);
        break;
    case CGW_CONVERSATION_MESSAGES:
        if (!identifier(input->arguments.messages.conversation_id)
            || !cursor(input->arguments.messages.before)
            || input->arguments.messages.turns < 1
            || input->arguments.messages.turns > 100)
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 100000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/conversations/%s/messages?before=",
                          input->arguments.messages.conversation_id)
            && encoded_cursor(request.path, sizeof(request.path),
                              &path_used, input->arguments.messages.before)
            && appendf(request.path, sizeof(request.path), &path_used,
                       "&include_has_versions=true&num_turns=%u",
                       input->arguments.messages.turns);
        break;
    case CGW_CONVERSATION_BATCH: {
        const char *const *ids = input->arguments.batch.conversation_ids;
        size_t count = input->arguments.batch.count;
        if (ids == NULL || count < 1 || count > 10) return CGW_INVALID_PARAMETERS;
        for (size_t i = 0; i < count; ++i) {
            if (!identifier(ids[i])) return CGW_INVALID_PARAMETERS;
            for (size_t j = 0; j < i; ++j) {
                if (strcmp(ids[i], ids[j]) == 0) return CGW_INVALID_PARAMETERS;
            }
        }
        request.method = CGW_POST; /* documented read-only batch retrieval */
        request.response_limit_bytes = 100000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/conversations/batch")
            && appendf(request.body, sizeof(request.body), &body_used,
                       "{\"conversation_ids\":[");
        for (size_t i = 0; written && i < count; ++i) {
            written = appendf(request.body, sizeof(request.body), &body_used,
                              "%s\"%s\"", i ? "," : "", ids[i]);
        }
        if (written)
            written = appendf(request.body, sizeof(request.body), &body_used, "]}");
        break;
    }
    case CGW_ACCOUNT_ARTIFACT:
        switch (input->arguments.artifact.kind) {
        case CGW_MEMORIES: value = "/backend-api/memories?include_memory_entries=true"; break;
        case CGW_CUSTOM_INSTRUCTIONS: value = "/backend-api/user_system_messages"; break;
        case CGW_SETTINGS: value = "/backend-api/settings"; break;
        case CGW_BETA_FEATURES: value = "/backend-api/settings/beta_features"; break;
        default: return CGW_INVALID_PARAMETERS;
        }
        request.response_limit_bytes = 20000000;
        written = appendf(request.path, sizeof(request.path), &path_used, "%s", value);
        break;
    case CGW_ASSET_OPEN:
        if (!identifier(input->arguments.asset.file_id)
            || !identifier(input->arguments.asset.parent_id)
            || (input->arguments.asset.scope != CGW_ASSET_CONVERSATION
                && input->arguments.asset.scope != CGW_ASSET_PROJECT))
            return CGW_INVALID_PARAMETERS;
        request.response_limit_bytes = 5000000;
        written = appendf(request.path, sizeof(request.path), &path_used,
                          "/backend-api/files/download/%s?%s=%s%s",
                          input->arguments.asset.file_id,
                          input->arguments.asset.scope == CGW_ASSET_PROJECT
                          ? "gizmo_id" : "conversation_id",
                          input->arguments.asset.parent_id,
                          input->arguments.asset.scope == CGW_ASSET_PROJECT
                          ? "" : "&inline=false");
        break;
    default:
        return CGW_INVALID_OPERATION;
    }

    if (!written) return CGW_CAPACITY_EXCEEDED;
    *output = request;
    return CGW_OK;
}

const char *cgw_status_name(cgw_status status)
{
    switch (status) {
    case CGW_OK: return "ok";
    case CGW_INVALID_OPERATION: return "invalid_operation";
    case CGW_INVALID_PARAMETERS: return "invalid_parameters";
    case CGW_CAPACITY_EXCEEDED: return "capacity_exceeded";
    }
    return "invalid_status";
}
