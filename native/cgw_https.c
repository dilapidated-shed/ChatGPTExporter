#include "cgw_https.h"

#include <curl/curl.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CGW_TOKEN_CAPACITY = 4096, CGW_DEFAULT_TIMEOUT_MS = 30000 };
struct cgw_fetcher {
    cgw_token_fn token;
    void *token_context;
    char workspace[257];
    int workspace_selected;
};

typedef struct {
    CURL *curl;
    cgw_chunk_fn sink;
    void *sink_context;
    size_t maximum;
    size_t received;
    int too_large;
    int sink_rejected;
    int secret_echo;
    const unsigned char *secret;
    size_t secret_length;
    size_t match_length;
    size_t failure[CGW_TOKEN_CAPACITY];
} write_state;

static void clear_secret(void *memory, size_t count)
{
    volatile unsigned char *bytes = (volatile unsigned char *)memory;
    while (count != 0) {
        *bytes++ = 0;
        --count;
    }
}

static int valid_identifier(const char *value)
{
    if (value == NULL || value[0] == '\0') return 0;
    size_t count = 0;
    for (; value[count] != '\0'; ++count) {
        unsigned char byte = (unsigned char)value[count];
        if (count >= 256) return 0;
        if (!((byte >= 'A' && byte <= 'Z')
              || (byte >= 'a' && byte <= 'z')
              || (byte >= '0' && byte <= '9')
              || byte == '_' || byte == '-')) return 0;
    }
    return count >= 1 && count <= 256;
}

static int valid_token(const char *value, size_t length)
{
    if (length < 16 || length >= CGW_TOKEN_CAPACITY
        || value[length] != '\0') return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)value[i];
        if (byte <= 32 || byte >= 127) return 0;
    }
    return 1;
}

/* KMP detects the whole bearer value even when an echo crosses TLS chunks.
 * The caller must still roll back its staging sink on any failed result. */
static void secret_match_init(write_state *state)
{
    for (size_t i = 1, j = 0; i < state->secret_length; ++i) {
        while (j != 0 && state->secret[i] != state->secret[j])
            j = state->failure[j - 1];
        if (state->secret[i] == state->secret[j]) ++j;
        state->failure[i] = j;
    }
}

static int secret_is_echoed(write_state *state,
                            const unsigned char *data, size_t size)
{
    size_t matched = state->match_length;
    for (size_t i = 0; i < size; ++i) {
        unsigned char byte = data[i];
        while (matched != 0 && byte != state->secret[matched])
            matched = state->failure[matched - 1];
        if (byte == state->secret[matched]) ++matched;
        if (matched == state->secret_length) {
            state->secret_echo = 1;
            return 1;
        }
    }
    state->match_length = matched;
    return 0;
}

static size_t receive_bytes(char *bytes, size_t element_size,
                            size_t element_count, void *context)
{
    write_state *state = (write_state *)context;
    if (element_size != 0 && element_count > SIZE_MAX / element_size) {
        state->too_large = 1;
        return 0;
    }
    size_t amount = element_size * element_count;
    if (amount > state->maximum - state->received) {
        state->too_large = 1;
        return 0;
    }
    state->received += amount;

    /* Detect a reflected token before publishing any bytes in this chunk. */
    if (secret_is_echoed(state, (const unsigned char *)bytes, amount))
        return 0;

    long status = 0;
    if (curl_easy_getinfo(state->curl, CURLINFO_RESPONSE_CODE, &status)
        != CURLE_OK) {
        state->sink_rejected = 1;
        return 0;
    }
    /* Error and redirect bodies never enter the caller's archive staging. */
    if (status < 200 || status > 299) return amount;

    if (amount != 0
        && !state->sink(state->sink_context,
                        (const unsigned char *)bytes, amount)) {
        state->sink_rejected = 1;
        return 0;
    }
    return amount;
}

static int add_header(struct curl_slist **headers, const char *line)
{
    struct curl_slist *next = curl_slist_append(*headers, line);
    if (next == NULL) return 0;
    *headers = next;
    return 1;
}

static void clear_headers(struct curl_slist *headers)
{
    for (struct curl_slist *current = headers;
         current != NULL; current = current->next) {
        if (current->data != NULL) {
            if (strncmp(current->data, "Authorization:", 14) == 0
                || strncmp(current->data, "X-Authorization:", 16) == 0) {
                clear_secret(current->data, strlen(current->data));
            }
        }
    }
    curl_slist_free_all(headers);
}

cgw_fetcher *cgw_fetcher_open(cgw_token_fn token, void *token_context,
                             const char *workspace_id)
{
    if (workspace_id != NULL && !valid_identifier(workspace_id)) return NULL;
    cgw_fetcher *client = (cgw_fetcher *)calloc(1, sizeof(*client));
    if (client == NULL) return NULL;
    client->token = token;
    client->token_context = token_context;
    if (workspace_id != NULL) {
        memcpy(client->workspace, workspace_id, strlen(workspace_id) + 1);
        client->workspace_selected = 1;
    }
    return client;
}

void cgw_fetcher_close(cgw_fetcher *client)
{
    if (client != NULL) {
        clear_secret(client, sizeof(*client));
        free(client);
    }
}

cgw_fetch_result cgw_fetcher_execute(cgw_fetcher *client,
                                     const cgw_input *input,
                                     cgw_fetch_options options,
                                     cgw_chunk_fn sink,
                                     void *sink_context,
                                     cgw_fetch_observation *observation)
{
    if (observation != NULL) memset(observation, 0, sizeof(*observation));
    if (client == NULL || input == NULL || sink == NULL
        || options.timeout_ms > 120000)
        return CGW_FETCH_INVALID_INPUT;

    cgw_http_request plan;
    if (cgw_resolve(input, &plan) != CGW_OK || !plan.requires_authentication)
        return CGW_FETCH_INVALID_INPUT;
    if (options.response_cap_bytes > plan.response_limit_bytes)
        return CGW_FETCH_INVALID_INPUT;
    size_t maximum = options.response_cap_bytes
        ? options.response_cap_bytes : plan.response_limit_bytes;
    if (maximum == 0) return CGW_FETCH_INVALID_INPUT;
    if (client->token == NULL) return CGW_FETCH_AUTH_UNAVAILABLE;

    char token[CGW_TOKEN_CAPACITY] = {0};
    size_t token_length = 0;
    int granted = client->token(client->token_context, token,
                                sizeof(token), &token_length);
    if (!granted) {
        clear_secret(token, sizeof(token));
        return CGW_FETCH_AUTH_UNAVAILABLE;
    }
    if (token_length >= sizeof(token)
        || !valid_token(token, token_length)) {
        clear_secret(token, sizeof(token));
        return CGW_FETCH_TOKEN_INVALID;
    }

    char authorization[CGW_TOKEN_CAPACITY + 32] = {0};
    char xauthorization[CGW_TOKEN_CAPACITY + 32] = {0};
    char account_header[320] = {0};
    snprintf(authorization, sizeof(authorization), "Authorization: Bearer %s",
             token);
    snprintf(xauthorization, sizeof(xauthorization),
             "X-Authorization: Bearer %s", token);
    if (client->workspace_selected)
        snprintf(account_header, sizeof(account_header),
                 "ChatGPT-Account-Id: %s", client->workspace);

    cgw_fetch_result result = CGW_FETCH_NETWORK_FAILURE;
    struct curl_slist *headers = NULL;
    CURL *curl = NULL;
#ifdef CGW_TEST_LOCAL_TLS
    struct curl_slist *connect_to = NULL;
#endif
    if (!add_header(&headers, "Accept: application/json")
        || !add_header(&headers, "Cache-Control: no-store")
        || !add_header(&headers, authorization)
        || !add_header(&headers, xauthorization)
        || (client->workspace_selected && !add_header(&headers, account_header))
        || (plan.method == CGW_POST
            && !add_header(&headers, "Content-Type: application/json"))) {
        goto cleanup;
    }
    clear_secret(authorization, sizeof(authorization));
    clear_secret(xauthorization, sizeof(xauthorization));

    curl = curl_easy_init();
    if (curl == NULL) goto cleanup;
    char url[CGW_PATH_CAPACITY + 32] = {0};
    if (snprintf(url, sizeof(url), "https://chatgpt.com%s", plan.path)
        >= (int)sizeof(url)) goto cleanup;

    write_state receiving = {0};
    receiving.curl = curl;
    receiving.sink = sink;
    receiving.sink_context = sink_context;
    receiving.maximum = maximum;
    receiving.secret = (const unsigned char *)token;
    receiving.secret_length = token_length;
    secret_match_init(&receiving);

#define REQUIRE_CURL(option, value) do { \
        if (curl_easy_setopt(curl, option, value) != CURLE_OK) goto cleanup; \
    } while (0)
    REQUIRE_CURL(CURLOPT_URL, url);
    REQUIRE_CURL(CURLOPT_HTTPHEADER, headers);
    REQUIRE_CURL(CURLOPT_PROTOCOLS_STR, "https");
    REQUIRE_CURL(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    REQUIRE_CURL(CURLOPT_FOLLOWLOCATION, 0L);
    REQUIRE_CURL(CURLOPT_MAXREDIRS, 0L);
    REQUIRE_CURL(CURLOPT_SSL_VERIFYPEER, 1L);
    REQUIRE_CURL(CURLOPT_SSL_VERIFYHOST, 2L);
    REQUIRE_CURL(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    REQUIRE_CURL(CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    REQUIRE_CURL(CURLOPT_PROXY, "");
    REQUIRE_CURL(CURLOPT_NOPROXY, "*");
    REQUIRE_CURL(CURLOPT_NOSIGNAL, 1L);
    REQUIRE_CURL(CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    REQUIRE_CURL(CURLOPT_TIMEOUT_MS,
                 (long)(options.timeout_ms ? options.timeout_ms
                                          : CGW_DEFAULT_TIMEOUT_MS));
    REQUIRE_CURL(CURLOPT_WRITEFUNCTION, receive_bytes);
    REQUIRE_CURL(CURLOPT_WRITEDATA, &receiving);
    if (plan.method == CGW_POST) {
        REQUIRE_CURL(CURLOPT_POST, 1L);
        REQUIRE_CURL(CURLOPT_POSTFIELDS, plan.body);
        REQUIRE_CURL(CURLOPT_POSTFIELDSIZE, (long)strlen(plan.body));
    } else {
        REQUIRE_CURL(CURLOPT_HTTPGET, 1L);
    }
#ifdef CGW_TEST_LOCAL_TLS
    /* This test-only build must never be linked into an APK/release. TLS
     * still verifies chatgpt.com against an ephemeral fixture-only CA. */
    connect_to = curl_slist_append(connect_to,
                 "chatgpt.com:443:127.0.0.1:18443");
    if (connect_to == NULL) goto cleanup;
    REQUIRE_CURL(CURLOPT_CONNECT_TO, connect_to);
    REQUIRE_CURL(CURLOPT_CAINFO, "build/fixture-cert.pem");
#endif
#undef REQUIRE_CURL

    CURLcode performed = curl_easy_perform(curl);
    long status = 0;
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    if (observation != NULL) {
        observation->http_status = status >= 100 && status <= 599
            ? (unsigned)status : 0;
        observation->received_bytes = receiving.received;
    }

    if (receiving.too_large) {
        result = CGW_FETCH_RESPONSE_TOO_LARGE;
    } else if (receiving.secret_echo) {
        result = CGW_FETCH_SECRET_ECHO;
    } else if (receiving.sink_rejected) {
        result = CGW_FETCH_SINK_REJECTED;
    } else if (performed == CURLE_PEER_FAILED_VERIFICATION
               || performed == CURLE_SSL_CONNECT_ERROR
               || performed == CURLE_SSL_CACERT_BADFILE) {
        result = CGW_FETCH_TLS_FAILURE;
    } else if (performed != CURLE_OK) {
        result = CGW_FETCH_NETWORK_FAILURE;
    } else if (status == 401) {
        result = CGW_FETCH_AUTH_REQUIRED;
    } else if (status == 403) {
        result = CGW_FETCH_WORKSPACE_FORBIDDEN;
    } else if (status == 429) {
        result = CGW_FETCH_RATE_LIMITED;
    } else if (status < 200 || status > 299) {
        result = CGW_FETCH_HTTP_FAILURE;
    } else {
        result = CGW_FETCH_OK;
    }
    if (observation != NULL) {
        observation->retryable = (result == CGW_FETCH_NETWORK_FAILURE
                                 || result == CGW_FETCH_RATE_LIMITED
                                 || status == 408 || status >= 500);
    }

cleanup:
    /* Free cURL before wiping borrowed POST/body/header source buffers. */
    if (curl != NULL) curl_easy_cleanup(curl);
#ifdef CGW_TEST_LOCAL_TLS
    curl_slist_free_all(connect_to);
#endif
    clear_headers(headers);
    clear_secret(token, sizeof(token));
    clear_secret(authorization, sizeof(authorization));
    clear_secret(xauthorization, sizeof(xauthorization));
    clear_secret(account_header, sizeof(account_header));
    clear_secret(&plan, sizeof(plan));
    return result;
}

const char *cgw_fetch_result_name(cgw_fetch_result result)
{
    switch (result) {
    case CGW_FETCH_OK: return "ok";
    case CGW_FETCH_INVALID_INPUT: return "invalid_input";
    case CGW_FETCH_AUTH_UNAVAILABLE: return "auth_unavailable";
    case CGW_FETCH_TOKEN_INVALID: return "token_invalid";
    case CGW_FETCH_NETWORK_FAILURE: return "network_failure";
    case CGW_FETCH_TLS_FAILURE: return "tls_failure";
    case CGW_FETCH_HTTP_FAILURE: return "http_failure";
    case CGW_FETCH_AUTH_REQUIRED: return "auth_required";
    case CGW_FETCH_WORKSPACE_FORBIDDEN: return "workspace_forbidden";
    case CGW_FETCH_RATE_LIMITED: return "rate_limited";
    case CGW_FETCH_RESPONSE_TOO_LARGE: return "response_too_large";
    case CGW_FETCH_SINK_REJECTED: return "sink_rejected";
    case CGW_FETCH_SECRET_ECHO: return "secret_echo";
    }
    return "unknown_result";
}
