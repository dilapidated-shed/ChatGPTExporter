#include "cgw_webview_bridge.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CGW_WEBVIEW_NONCE_BYTES = 16 };
enum { CGW_WEBVIEW_MAX_CHUNKS = 16384 };

typedef struct {
    const char *text;
    size_t length;
} field;

struct cgw_webview_bridge {
    cgw_webview_bridge_config config;
    uint64_t navigation_id;
    uint64_t next_request_id;
    uint64_t active_request_id;
    uint64_t expected_sequence;
    size_t maximum_bytes;
    size_t received_bytes;
    unsigned active_status;
    char nonce[CGW_WEBVIEW_NONCE_HEX_LENGTH + 1];
    cgw_webview_chunk_fn sink;
    void *sink_context;
    int document_verified;
    int port_ready;
    int active;
    int complete;
    int retryable;
};

static const char origin[] = "https://chatgpt.com";
static const char hexadecimal[] = "0123456789abcdef";

static void clear_command(cgw_webview_command *command)
{
    if (command != NULL) memset(command, 0, sizeof(*command));
}

static int same(const field *value, const char *literal)
{
    size_t length = strlen(literal);
    return value->length == length && memcmp(value->text, literal, length) == 0;
}

static int decimal_u64(const field *value, uint64_t *output)
{
    if (value->length == 0) return 0;
    uint64_t result = 0;
    for (size_t i = 0; i < value->length; ++i) {
        unsigned char byte = (unsigned char)value->text[i];
        if (byte < '0' || byte > '9') return 0;
        unsigned digit = (unsigned)(byte - '0');
        if (result > (UINT64_MAX - digit) / 10) return 0;
        result = result * 10 + digit;
    }
    *output = result;
    return 1;
}

static int base64url_value(unsigned char byte)
{
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    if (byte == '-') return 62;
    if (byte == '_') return 63;
    return -1;
}

static int base64url_encode(const char *input, size_t length,
                            char *output, size_t capacity, size_t *written)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t out = 0;
    for (size_t i = 0; i < length; i += 3) {
        size_t remaining = length - i;
        unsigned value = (unsigned)(unsigned char)input[i] << 16;
        if (remaining > 1) value |= (unsigned)(unsigned char)input[i + 1] << 8;
        if (remaining > 2) value |= (unsigned)(unsigned char)input[i + 2];
        size_t count = remaining >= 3 ? 4 : remaining + 1;
        if (count > capacity - out) return 0;
        output[out++] = alphabet[(value >> 18) & 63];
        output[out++] = alphabet[(value >> 12) & 63];
        if (count > 2) output[out++] = alphabet[(value >> 6) & 63];
        if (count > 3) output[out++] = alphabet[value & 63];
    }
    *written = out;
    return 1;
}

static int base64url_decode(const field *input, unsigned char *output,
                            size_t capacity, size_t *written)
{
    if (input->length % 4 == 1) return 0;
    size_t out = 0;
    for (size_t i = 0; i < input->length; i += 4) {
        size_t remaining = input->length - i;
        size_t count = remaining >= 4 ? 4 : remaining;
        if (count < 2) return 0;
        int a = base64url_value((unsigned char)input->text[i]);
        int b = base64url_value((unsigned char)input->text[i + 1]);
        int c = count > 2 ? base64url_value((unsigned char)input->text[i + 2]) : 0;
        int d = count > 3 ? base64url_value((unsigned char)input->text[i + 3]) : 0;
        if (a < 0 || b < 0 || c < 0 || d < 0) return 0;
        size_t produced = count - 1;
        if (produced > capacity - out) return 0;
        output[out++] = (unsigned char)((a << 2) | (b >> 4));
        if (count > 2) output[out++] = (unsigned char)((b << 4) | (c >> 2));
        if (count > 3) output[out++] = (unsigned char)((c << 6) | d);
        if (count == 2 && (b & 15) != 0) return 0;
        if (count == 3 && (c & 3) != 0) return 0;
    }
    *written = out;
    return 1;
}

static int wire_fields(const char *wire, size_t length, field output[8])
{
    if (wire == NULL || length == 0 || length > CGW_WEBVIEW_MESSAGE_CAPACITY)
        return 0;
    size_t start = 0;
    unsigned count = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i != length && wire[i] != ';') continue;
        if (count >= 8) return 0;
        output[count].text = wire + start;
        output[count].length = i - start;
        ++count;
        start = i + 1;
    }
    return count == 8;
}

static void cancel_active(cgw_webview_bridge *bridge)
{
    bridge->active = 0;
    bridge->sink = NULL;
    bridge->sink_context = NULL;
    bridge->expected_sequence = 0;
}

static void revoke_document(cgw_webview_bridge *bridge)
{
    cancel_active(bridge);
    bridge->document_verified = 0;
    bridge->port_ready = 0;
    bridge->complete = 0;
    bridge->retryable = 0;
    bridge->received_bytes = 0;
    bridge->active_status = 0;
    memset(bridge->nonce, 0, sizeof(bridge->nonce));
}

cgw_webview_bridge *cgw_webview_bridge_open(
    const cgw_webview_bridge_config *config)
{
    if (config == NULL || config->entropy == NULL) return NULL;
    cgw_webview_bridge *bridge = calloc(1, sizeof(*bridge));
    if (bridge == NULL) return NULL;
    bridge->config = *config;
    bridge->navigation_id = 1;
    bridge->next_request_id = 1;
    return bridge;
}

void cgw_webview_bridge_close(cgw_webview_bridge *bridge)
{
    if (bridge == NULL) return;
    cancel_active(bridge);
    free(bridge);
}

cgw_webview_result cgw_webview_bridge_navigation_started(
    cgw_webview_bridge *bridge)
{
    if (bridge == NULL) return CGW_WEBVIEW_INVALID_ARGUMENT;
    int cancelled = bridge->active;
    if (bridge->active) {
        cancel_active(bridge);
        bridge->complete = 0;
    }
    if (bridge->navigation_id != UINT64_MAX) ++bridge->navigation_id;
    revoke_document(bridge);
    return cancelled ? CGW_WEBVIEW_NAVIGATION_CANCELLED : CGW_WEBVIEW_OK;
}

cgw_webview_result cgw_webview_bridge_document_committed(
    cgw_webview_bridge *bridge, const char *document_origin, int main_frame)
{
    if (bridge == NULL || document_origin == NULL)
        return CGW_WEBVIEW_INVALID_ARGUMENT;
    if (!main_frame) return CGW_WEBVIEW_FRAME_REJECTED;
    if (strcmp(document_origin, origin) != 0) {
        revoke_document(bridge);
        return CGW_WEBVIEW_ORIGIN_REJECTED;
    }
    /* A correct adapter reports navigation_started first.  Revoke the old
     * port anyway so a missed lifecycle callback cannot retain its authority. */
    revoke_document(bridge);
    bridge->document_verified = 1;
    bridge->port_ready = 0;
    return CGW_WEBVIEW_OK;
}

cgw_webview_result cgw_webview_bridge_port_ready(cgw_webview_bridge *bridge)
{
    if (bridge == NULL) return CGW_WEBVIEW_INVALID_ARGUMENT;
    if (!bridge->document_verified) return CGW_WEBVIEW_NOT_READY;
    bridge->port_ready = 1;
    return CGW_WEBVIEW_OK;
}

static int make_nonce(cgw_webview_bridge *bridge)
{
    unsigned char bytes[CGW_WEBVIEW_NONCE_BYTES];
    if (!bridge->config.entropy(bridge->config.entropy_context, bytes,
                                sizeof(bytes))) return 0;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bridge->nonce[2 * i] = hexadecimal[bytes[i] >> 4];
        bridge->nonce[2 * i + 1] = hexadecimal[bytes[i] & 15];
    }
    bridge->nonce[CGW_WEBVIEW_NONCE_HEX_LENGTH] = '\0';
    return 1;
}

cgw_webview_result cgw_webview_bridge_begin(
    cgw_webview_bridge *bridge, const cgw_input *input,
    cgw_webview_chunk_fn sink, void *sink_context,
    cgw_webview_command *command)
{
    clear_command(command);
    if (bridge == NULL || input == NULL || sink == NULL || command == NULL)
        return CGW_WEBVIEW_INVALID_ARGUMENT;
    if (!bridge->document_verified || !bridge->port_ready)
        return CGW_WEBVIEW_NOT_READY;
    if (bridge->active) return CGW_WEBVIEW_BUSY;
    if (bridge->next_request_id == UINT64_MAX) return CGW_WEBVIEW_ENTROPY_FAILURE;

    cgw_http_request request;
    if (cgw_resolve(input, &request) != CGW_OK)
        return CGW_WEBVIEW_REQUEST_REJECTED;
    if (!make_nonce(bridge)) return CGW_WEBVIEW_ENTROPY_FAILURE;

    size_t body_encoded = 0;
    size_t body_length = strlen(request.body);
    char body[CGW_BODY_CAPACITY * 2];
    if (!base64url_encode(request.body, body_length, body, sizeof(body),
                          &body_encoded)) return CGW_WEBVIEW_INVALID_ARGUMENT;
    int count = snprintf(command->wire, sizeof(command->wire),
                         "CGW1;request;%s;%llu;%s;%s;%.*s",
                         bridge->nonce,
                         (unsigned long long)bridge->next_request_id,
                         request.method == CGW_GET ? "GET" : "POST",
                         request.path, (int)body_encoded, body);
    if (count < 0 || (size_t)count >= sizeof(command->wire)) {
        memset(bridge->nonce, 0, sizeof(bridge->nonce));
        return CGW_WEBVIEW_INVALID_ARGUMENT;
    }

    bridge->active = 1;
    bridge->complete = 0;
    bridge->retryable = 0;
    bridge->active_request_id = bridge->next_request_id++;
    bridge->expected_sequence = 0;
    bridge->active_status = 0;
    bridge->received_bytes = 0;
    bridge->maximum_bytes = request.response_limit_bytes;
    if (bridge->config.response_cap_bytes != 0
        && bridge->config.response_cap_bytes < bridge->maximum_bytes)
        bridge->maximum_bytes = bridge->config.response_cap_bytes;
    bridge->sink = sink;
    bridge->sink_context = sink_context;
    command->navigation_id = bridge->navigation_id;
    command->request_id = bridge->active_request_id;
    memcpy(command->nonce, bridge->nonce, sizeof(command->nonce));
    return CGW_WEBVIEW_OK;
}

cgw_webview_result cgw_webview_bridge_receive(
    cgw_webview_bridge *bridge, const char *wire, size_t length)
{
    if (bridge == NULL) return CGW_WEBVIEW_INVALID_ARGUMENT;
    if (!bridge->active) return CGW_WEBVIEW_NO_ACTIVE_REQUEST;
    field parts[8];
    if (!wire_fields(wire, length, parts)
        || !same(&parts[0], "CGW1") || !same(&parts[1], "reply")
        || parts[2].length != CGW_WEBVIEW_NONCE_HEX_LENGTH
        || memcmp(parts[2].text, bridge->nonce, parts[2].length) != 0)
        return CGW_WEBVIEW_REPLAY_REJECTED;

    uint64_t request_id = 0, sequence = 0, status_number = 0, final_number = 0;
    if (!decimal_u64(&parts[3], &request_id)
        || !decimal_u64(&parts[4], &status_number)
        || !decimal_u64(&parts[5], &sequence)
        || !decimal_u64(&parts[6], &final_number)
        || request_id != bridge->active_request_id || status_number < 100
        || status_number > 599 || final_number > 1)
        return CGW_WEBVIEW_INVALID_WIRE;
    unsigned status = (unsigned)status_number;
    if (status < 200 || status >= 300) {
        if (sequence != 0 || final_number != 1 || parts[7].length != 0)
            return CGW_WEBVIEW_INVALID_WIRE;
        bridge->active_status = status;
        bridge->retryable = status == 429 || status >= 500;
        cancel_active(bridge);
        return CGW_WEBVIEW_HTTP_FAILURE;
    }
    if (sequence != bridge->expected_sequence
        || (bridge->active_status != 0 && bridge->active_status != status))
        return CGW_WEBVIEW_REPLAY_REJECTED;
    if (bridge->expected_sequence >= CGW_WEBVIEW_MAX_CHUNKS) {
        cancel_active(bridge);
        return CGW_WEBVIEW_RESPONSE_TOO_LARGE;
    }

    unsigned char decoded[CGW_WEBVIEW_MESSAGE_CAPACITY];
    size_t decoded_length = 0;
    if (!base64url_decode(&parts[7], decoded, sizeof(decoded), &decoded_length))
        return CGW_WEBVIEW_INVALID_WIRE;
    if (decoded_length > bridge->maximum_bytes - bridge->received_bytes) {
        cancel_active(bridge);
        return CGW_WEBVIEW_RESPONSE_TOO_LARGE;
    }
    if (decoded_length != 0 && !bridge->sink(bridge->sink_context, decoded,
                                             decoded_length)) {
        cancel_active(bridge);
        return CGW_WEBVIEW_SINK_REJECTED;
    }
    bridge->active_status = status;
    bridge->received_bytes += decoded_length;
    if (bridge->expected_sequence == UINT64_MAX) {
        cancel_active(bridge);
        return CGW_WEBVIEW_INVALID_WIRE;
    }
    ++bridge->expected_sequence;
    if (final_number == 1) {
        bridge->complete = 1;
        cancel_active(bridge);
    }
    return CGW_WEBVIEW_OK;
}

void cgw_webview_bridge_observation(const cgw_webview_bridge *bridge,
                                    cgw_webview_observation *output)
{
    if (output == NULL) return;
    memset(output, 0, sizeof(*output));
    if (bridge == NULL) return;
    output->navigation_id = bridge->navigation_id;
    output->request_id = bridge->active ? bridge->active_request_id
                                        : bridge->next_request_id - 1;
    output->http_status = bridge->active_status;
    output->received_bytes = bridge->received_bytes;
    output->complete = bridge->complete;
    output->retryable = bridge->retryable;
}

const char *cgw_webview_result_name(cgw_webview_result result)
{
    switch (result) {
    case CGW_WEBVIEW_OK: return "ok";
    case CGW_WEBVIEW_INVALID_ARGUMENT: return "invalid_argument";
    case CGW_WEBVIEW_ORIGIN_REJECTED: return "origin_rejected";
    case CGW_WEBVIEW_FRAME_REJECTED: return "frame_rejected";
    case CGW_WEBVIEW_NOT_READY: return "not_ready";
    case CGW_WEBVIEW_BUSY: return "busy";
    case CGW_WEBVIEW_REQUEST_REJECTED: return "request_rejected";
    case CGW_WEBVIEW_ENTROPY_FAILURE: return "entropy_failure";
    case CGW_WEBVIEW_NO_ACTIVE_REQUEST: return "no_active_request";
    case CGW_WEBVIEW_INVALID_WIRE: return "invalid_wire";
    case CGW_WEBVIEW_REPLAY_REJECTED: return "replay_rejected";
    case CGW_WEBVIEW_RESPONSE_TOO_LARGE: return "response_too_large";
    case CGW_WEBVIEW_HTTP_FAILURE: return "http_failure";
    case CGW_WEBVIEW_SINK_REJECTED: return "sink_rejected";
    case CGW_WEBVIEW_NAVIGATION_CANCELLED: return "navigation_cancelled";
    }
    return "unknown_webview_result";
}
