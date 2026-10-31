#include "cgw_webview_bridge.h"

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

typedef struct {
    unsigned next;
    int enabled;
} entropy_source;

static int entropy(void *context, unsigned char *bytes, size_t length)
{
    entropy_source *source = context;
    if (!source->enabled) return 0;
    for (size_t i = 0; i < length; ++i)
        bytes[i] = (unsigned char)(source->next++);
    return 1;
}

typedef struct {
    unsigned char bytes[64];
    size_t length;
    int accept;
} receipt;

static int collect(void *context, const unsigned char *bytes, size_t length)
{
    receipt *output = context;
    if (length > sizeof(output->bytes) - output->length) return 0;
    memcpy(output->bytes + output->length, bytes, length);
    output->length += length;
    return output->accept;
}

static cgw_webview_bridge *open_bridge(entropy_source *source, size_t cap)
{
    cgw_webview_bridge_config config = {
        .entropy = entropy,
        .entropy_context = source,
        .response_cap_bytes = cap
    };
    cgw_webview_bridge *bridge = cgw_webview_bridge_open(&config);
    check(bridge != NULL, "bridge opens with platform entropy");
    return bridge;
}

static void ready(cgw_webview_bridge *bridge)
{
    check(cgw_webview_bridge_document_committed(bridge, "https://chatgpt.com", 1)
              == CGW_WEBVIEW_OK,
          "same-origin main document is accepted");
    check(cgw_webview_bridge_port_ready(bridge) == CGW_WEBVIEW_OK,
          "current document port is accepted");
}

static cgw_input accounts_request(void)
{
    cgw_input input = {0};
    input.operation = CGW_ACCOUNTS_LIST;
    return input;
}

static cgw_webview_command begin(cgw_webview_bridge *bridge, receipt *output)
{
    cgw_webview_command command;
    cgw_input input = accounts_request();
    check(cgw_webview_bridge_begin(bridge, &input, collect, output, &command)
              == CGW_WEBVIEW_OK,
          "allowlisted request is prepared");
    return command;
}

static cgw_webview_result reply(cgw_webview_bridge *bridge,
                                const cgw_webview_command *command,
                                unsigned status, unsigned long long sequence,
                                int final, const char *base64url)
{
    char wire[CGW_WEBVIEW_MESSAGE_CAPACITY];
    int count = snprintf(wire, sizeof(wire), "CGW1;reply;%s;%llu;%u;%llu;%d;%s",
                         command->nonce,
                         (unsigned long long)command->request_id, status,
                         sequence, final, base64url);
    check(count > 0 && (size_t)count < sizeof(wire), "reply remains bounded");
    return cgw_webview_bridge_receive(bridge, wire, (size_t)count);
}

static void lifecycle_and_allowlist(void)
{
    entropy_source source = { .enabled = 1 };
    receipt output = { .accept = 1 };
    cgw_webview_bridge *bridge = open_bridge(&source, 0);
    cgw_input input = accounts_request();
    cgw_webview_command command;

    check(cgw_webview_bridge_port_ready(bridge) == CGW_WEBVIEW_NOT_READY,
          "port cannot precede a verified document");
    check(cgw_webview_bridge_document_committed(bridge, "https://chatgpt.com", 0)
              == CGW_WEBVIEW_FRAME_REJECTED,
          "iframe document is rejected");
    check(cgw_webview_bridge_document_committed(bridge, "https://evil.example", 1)
              == CGW_WEBVIEW_ORIGIN_REJECTED,
          "wrong origin is rejected");
    check(cgw_webview_bridge_begin(bridge, &input, collect, &output, &command)
              == CGW_WEBVIEW_NOT_READY,
          "request cannot precede page verification");

    ready(bridge);
    check(cgw_webview_bridge_begin(bridge, &input, collect, &output, &command)
              == CGW_WEBVIEW_OK,
          "allowlisted authenticated operation starts");
    check(strstr(command.wire, "CGW1;request;") == command.wire,
          "command uses typed protocol prefix");
    check(strstr(command.wire, ";GET;/backend-api/accounts/check/v4-2023-04-27;")
              != NULL,
          "command contains only resolver-selected relative route");

    cgw_input invalid = {0};
    cgw_webview_command rejected;
    check(cgw_webview_bridge_begin(bridge, &invalid, collect, &output, &rejected)
              == CGW_WEBVIEW_BUSY,
          "second request cannot race active request");
    check(cgw_webview_bridge_navigation_started(bridge)
              == CGW_WEBVIEW_NAVIGATION_CANCELLED,
          "navigation cancels active request");
    check(cgw_webview_bridge_receive(bridge, "not-a-wire", 10)
              == CGW_WEBVIEW_NO_ACTIVE_REQUEST,
          "cancelled page cannot deliver a response");
    check(cgw_webview_bridge_navigation_started(bridge) == CGW_WEBVIEW_OK,
          "idle navigation is recorded");
    ready(bridge);
    check(cgw_webview_bridge_begin(bridge, &invalid, collect, &output, &rejected)
              == CGW_WEBVIEW_REQUEST_REJECTED,
          "untyped operation does not reach WebView");
    cgw_webview_bridge_close(bridge);
}

static void reply_validation(void)
{
    entropy_source source = { .enabled = 1 };
    receipt output = { .accept = 1 };
    cgw_webview_bridge *bridge = open_bridge(&source, 0);
    ready(bridge);
    cgw_webview_command command = begin(bridge, &output);

    char wrong_nonce[CGW_WEBVIEW_NONCE_HEX_LENGTH + 1];
    memcpy(wrong_nonce, command.nonce, sizeof(wrong_nonce));
    wrong_nonce[0] = wrong_nonce[0] == '0' ? '1' : '0';
    char forged[256];
    int count = snprintf(forged, sizeof(forged), "CGW1;reply;%s;%llu;200;0;1;eA",
                         wrong_nonce, (unsigned long long)command.request_id);
    check(count > 0 && (size_t)count < sizeof(forged), "forged reply fits test buffer");
    check(cgw_webview_bridge_receive(bridge, forged, (size_t)count)
              == CGW_WEBVIEW_REPLAY_REJECTED,
          "wrong nonce is rejected before sink");
    check(output.length == 0, "forged reply publishes no bytes");
    check(reply(bridge, &command, 200, 0, 0, "aGVsbG8") == CGW_WEBVIEW_OK,
          "first ordered chunk accepted");
    check(reply(bridge, &command, 200, 0, 0, "aGVsbG8")
              == CGW_WEBVIEW_REPLAY_REJECTED,
          "replayed sequence rejected");
    check(reply(bridge, &command, 200, 1, 1, "IHdvcmxk") == CGW_WEBVIEW_OK,
          "final ordered chunk accepted");
    check(output.length == 11 && memcmp(output.bytes, "hello world", 11) == 0,
          "sink receives decoded bounded response only once");

    cgw_webview_observation observation;
    cgw_webview_bridge_observation(bridge, &observation);
    check(observation.complete && observation.http_status == 200
              && observation.received_bytes == 11,
          "completed response records status and byte count");
    check(cgw_webview_bridge_receive(bridge, "CGW1;reply;;;;;;", 19)
              == CGW_WEBVIEW_NO_ACTIVE_REQUEST,
          "completed request cannot be replayed");
    cgw_webview_bridge_close(bridge);
}

static void error_and_size_boundaries(void)
{
    entropy_source source = { .enabled = 1 };
    receipt output = { .accept = 1 };
    cgw_webview_bridge *bridge = open_bridge(&source, 5);
    ready(bridge);
    cgw_webview_command command = begin(bridge, &output);
    check(reply(bridge, &command, 200, 0, 1, "YWJjZGVm")
              == CGW_WEBVIEW_RESPONSE_TOO_LARGE,
          "configured response cap rejects oversize decoded payload");
    check(output.length == 0, "oversize payload reaches no sink");

    check(cgw_webview_bridge_navigation_started(bridge) == CGW_WEBVIEW_OK,
          "new navigation clears rejected state");
    ready(bridge);
    command = begin(bridge, &output);
    check(reply(bridge, &command, 429, 0, 1, "") == CGW_WEBVIEW_HTTP_FAILURE,
          "retryable HTTP failure is terminal and data-free");
    cgw_webview_observation observation;
    cgw_webview_bridge_observation(bridge, &observation);
    check(observation.http_status == 429 && observation.retryable
              && !observation.complete,
          "HTTP failure remains observable without its payload");

    check(cgw_webview_bridge_navigation_started(bridge) == CGW_WEBVIEW_OK,
          "idle navigation resets port after HTTP failure");
    ready(bridge);
    command = begin(bridge, &output);
    check(reply(bridge, &command, 200, 0, 1, "*") == CGW_WEBVIEW_INVALID_WIRE,
          "non-base64url payload rejected");
    check(cgw_webview_bridge_navigation_started(bridge)
              == CGW_WEBVIEW_NAVIGATION_CANCELLED,
          "bad wire leaves request cancellable by navigation");
    cgw_webview_bridge_close(bridge);
}

static void entropy_and_sink_failures(void)
{
    entropy_source source = { .enabled = 0 };
    receipt output = { .accept = 1 };
    cgw_webview_bridge *bridge = open_bridge(&source, 0);
    ready(bridge);
    cgw_input input = accounts_request();
    cgw_webview_command command;
    check(cgw_webview_bridge_begin(bridge, &input, collect, &output, &command)
              == CGW_WEBVIEW_ENTROPY_FAILURE,
          "request nonce requires platform entropy");
    cgw_webview_bridge_close(bridge);

    source.enabled = 1;
    output.accept = 0;
    bridge = open_bridge(&source, 0);
    ready(bridge);
    command = begin(bridge, &output);
    check(reply(bridge, &command, 200, 0, 1, "eA")
              == CGW_WEBVIEW_SINK_REJECTED,
          "sink refusal aborts active response");
    cgw_webview_bridge_close(bridge);
}

int main(void)
{
    lifecycle_and_allowlist();
    reply_validation();
    error_and_size_boundaries();
    entropy_and_sink_failures();
    check(strcmp(cgw_webview_result_name(CGW_WEBVIEW_REPLAY_REJECTED),
                 "replay_rejected") == 0,
          "result names remain stable for platform logs");
    printf("PASS %u bounded WebView bridge assertions (host-only, no WebView)\n",
           assertions);
    return 0;
}
