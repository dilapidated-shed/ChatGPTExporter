#include "cgw_https.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char fixture_token[] = "synthetic-fixture-token-123456";
static unsigned assertions;
static unsigned token_calls;
static int token_mode;

typedef struct {
    size_t bytes;
    unsigned calls;
    int fail;
    char text[16384];
} staged;

static void check(int condition, const char *name)
{
    ++assertions;
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static int token_provider(void *unused, char *buffer, size_t cap, size_t *length)
{
    (void)unused;
    ++token_calls;
    if (token_mode == 1) return 0;
    const char *token = token_mode == 2
        ? "synthetic-invalid\nheader-injection" : fixture_token;
    size_t size = strlen(token);
    if (size >= cap) return 0;
    memcpy(buffer, token, size + 1);
    *length = size;
    return 1;
}

static int staging_sink(void *context, const unsigned char *bytes, size_t count)
{
    staged *target = (staged *)context;
    if (target->fail || count > sizeof(target->text) - target->bytes) return 0;
    memcpy(target->text + target->bytes, bytes, count);
    target->bytes += count;
    ++target->calls;
    return 1;
}

static cgw_input share(const char *id)
{
    cgw_input input = {0};
    input.operation = CGW_SHARED_DETAIL;
    input.arguments.detail.id = id;
    return input;
}

static cgw_fetch_result request(cgw_fetcher *client, cgw_input input,
                               size_t maximum, staged *out,
                               cgw_fetch_observation *observation)
{
    cgw_fetch_options options = { .response_cap_bytes = maximum };
    return cgw_fetcher_execute(client, &input, options,
                              staging_sink, out, observation);
}

static void preflight(void)
{
    check(cgw_fetcher_open(token_provider, NULL, "account/evil") == NULL,
          "reject workspace path");
    check(cgw_fetcher_open(token_provider, NULL, "account\nHeader: true") == NULL,
          "reject workspace header injection");
    check(cgw_fetcher_open(token_provider, NULL, "") == NULL,
          "reject empty workspace");

    cgw_fetcher *client = cgw_fetcher_open(token_provider, NULL, "account_A");
    check(client != NULL, "valid account-scoped authority");
    staged output = {0};
    cgw_fetch_observation observed = {0};
    cgw_input bad = {0};
    bad.operation = CGW_CONVERSATION_PAGE;
    bad.arguments.conversation_page.limit = 101;
    unsigned before = token_calls;
    check(request(client, bad, 0, &output, &observed)
          == CGW_FETCH_INVALID_INPUT, "reject malformed page before token");
    check(token_calls == before, "invalid request never asks for token");

    bad.arguments.conversation_page.limit = 2;
    check(request(client, bad, 20000001, &output, &observed)
          == CGW_FETCH_INVALID_INPUT, "caller may not raise provider bound");
    check(token_calls == before, "invalid cap never asks for token");

    cgw_fetch_options options = { .timeout_ms = 120001 };
    check(cgw_fetcher_execute(client, &bad, options,
                              staging_sink, &output, &observed)
          == CGW_FETCH_INVALID_INPUT, "reject unbounded timeout");
    check(token_calls == before, "invalid timeout never asks for token");
    cgw_fetcher_close(client);
}

static void execute(void)
{
    cgw_fetcher *client = cgw_fetcher_open(token_provider, NULL, "account_A");
    check(client != NULL, "client created");
    staged buffer = {0};
    cgw_fetch_observation observed = {0};

    cgw_input listing = {0};
    listing.operation = CGW_CONVERSATION_PAGE;
    listing.arguments.conversation_page.offset = 0;
    listing.arguments.conversation_page.limit = 2;
    check(request(client, listing, 0, &buffer, &observed) == CGW_FETCH_OK,
          "HTTPS listing result");
    check(observed.http_status == 200 && observed.retryable == 0,
          "HTTPS status recorded");
    check(strcmp(buffer.text, "{\"items\":[],\"total\":0}") == 0,
          "exact listing body");
    check(buffer.bytes == observed.received_bytes && buffer.calls >= 1,
          "bounded streaming sink");

    memset(&buffer, 0, sizeof(buffer));
    const char *ids[] = { "one", "two" };
    cgw_input batch = {0};
    batch.operation = CGW_CONVERSATION_BATCH;
    batch.arguments.batch.conversation_ids = ids;
    batch.arguments.batch.count = 2;
    check(request(client, batch, 0, &buffer, &observed) == CGW_FETCH_OK,
          "HTTPS bounded batch POST");
    check(strcmp(buffer.text, "{\"batch\":true}") == 0,
          "exact POST payload accepted by fixture");

    memset(&buffer, 0, sizeof(buffer));
    check(request(client, share("oversized"), 64, &buffer, &observed)
          == CGW_FETCH_RESPONSE_TOO_LARGE, "response cap enforced");
    check(buffer.bytes == 0, "oversized first chunk not staged");

    memset(&buffer, 0, sizeof(buffer));
    check(request(client, share("redirect"), 0, &buffer, &observed)
          == CGW_FETCH_HTTP_FAILURE, "redirect rejected");
    check(observed.http_status == 302 && buffer.bytes == 0,
          "no redirect or body staging");

    check(request(client, share("auth"), 0, &buffer, &observed)
          == CGW_FETCH_AUTH_REQUIRED, "401 mapped to auth required");
    check(observed.http_status == 401 && observed.retryable == 0,
          "401 not retried");
    check(request(client, share("forbid"), 0, &buffer, &observed)
          == CGW_FETCH_WORKSPACE_FORBIDDEN, "403 workspace failure");
    check(request(client, share("rate"), 0, &buffer, &observed)
          == CGW_FETCH_RATE_LIMITED, "429 mapped to rate limiting");
    check(observed.retryable == 1, "rate limit retry hint without retry");
    check(request(client, share("server-error"), 0, &buffer, &observed)
          == CGW_FETCH_HTTP_FAILURE, "503 returns HTTP failure");
    check(observed.http_status == 503 && observed.retryable == 1,
          "server error retry hint");

    memset(&buffer, 0, sizeof(buffer));
    check(request(client, share("echo-token"), 0, &buffer, &observed)
          == CGW_FETCH_SECRET_ECHO, "reflected bearer rejected");
    check(strstr(buffer.text, fixture_token) == NULL,
          "reflected bearer did not enter accepted staging");

    memset(&buffer, 0, sizeof(buffer));
    buffer.fail = 1;
    check(request(client, listing, 0, &buffer, &observed)
          == CGW_FETCH_SINK_REJECTED, "sink backpressure stops request");

    token_mode = 1;
    unsigned before = token_calls;
    check(request(client, listing, 0, &buffer, &observed)
          == CGW_FETCH_AUTH_UNAVAILABLE, "auth callback refusal");
    check(token_calls == before + 1, "auth callback once per request");
    token_mode = 2;
    check(request(client, listing, 0, &buffer, &observed)
          == CGW_FETCH_TOKEN_INVALID, "refuse header CRLF from provider");
    token_mode = 0;
    cgw_fetcher_close(client);
}

int main(void)
{
    preflight();
    execute();
    printf("PASS %u fixed-origin TLS HTTP controls (fixture credentials only)\n",
           assertions);
    return 0;
}
