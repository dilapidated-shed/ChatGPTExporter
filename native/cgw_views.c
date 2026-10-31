#include "cgw_views.h"
#include "cgw_json.h"
#include "cgw_sha256.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Application-owned C11 normalizer. The TypeScript normalizer remains a
 * separate oracle. Raw JSON is never replaced by these derived views. */
enum { OUTPUT_LIMIT = 33554432, NODE_LIMIT = 2048 };
typedef struct { char *p; size_t n, cap; int failed; } buffer;
typedef struct { size_t node, message; char id[257], parent[257]; int selected; buffer md; } node;
typedef struct { cj_document d; buffer findings; unsigned finding_count; } normalizer;
static void put(buffer *b, const char *s, size_t n)
{
    if (b->failed || n > OUTPUT_LIMIT - b->n) { b->failed = 1; return; }
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) { b->failed = 1; return; }
        b->p = p; b->cap = cap;
    }
    memcpy(b->p + b->n, s, n); b->n += n; b->p[b->n] = 0;
}
static void str(buffer *b, const char *s) { put(b, s, strlen(s)); }
static void quoted(buffer *b, const char *s, size_t n)
{
    str(b, "\"");
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { str(b, "\\"); put(b, s + i, 1); }
        else if (c == '\b') str(b, "\\b");
        else if (c == '\f') str(b, "\\f");
        else if (c == '\n') str(b, "\\n");
        else if (c == '\r') str(b, "\\r");
        else if (c == '\t') str(b, "\\t");
        else if (c < 32) { char escape[7]; (void)snprintf(escape, sizeof(escape), "\\u%04x", c); str(b, escape); }
        else put(b, s + i, 1);
    }
    str(b, "\"");
}
static void quote(buffer *b, const char *s) { quoted(b, s, strlen(s)); }
static int is_kind(cj_document *d, size_t t, cj_kind k) { return t < d->count && d->tokens[t].kind == k; }
static size_t member(cj_document *d, size_t t, const char *name) { return cj_member(d, t, name); }
static char *text(cj_document *d, size_t t, size_t *length)
{
    if (!is_kind(d, t, CJ_STRING)) return NULL;
    size_t cap = d->tokens[t].end - d->tokens[t].begin + 1;
    char *p = malloc(cap);
    if (!p || !cj_text_bytes(d, t, p, cap, length)) { free(p); return NULL; }
    return p;
}
static char *cstring(cj_document *d, size_t t)
{
    size_t n; char *s = text(d, t, &n);
    if (s && memchr(s, 0, n)) { free(s); return NULL; }
    return s;
}
static void spaces(buffer *b, unsigned count) { while (count--) str(b, " "); }
static void raw(buffer *b, cj_document *d, size_t t, int pretty, unsigned depth)
{
    if (t >= d->count) { str(b, "null"); return; }
    cj_token token = d->tokens[t];
    if (token.kind == CJ_STRING) {
        size_t n; char *s = text(d, t, &n);
        if (!s) b->failed = 1; else { quoted(b, s, n); free(s); }
    } else if (token.kind == CJ_OBJECT || token.kind == CJ_ARRAY) {
        int object = token.kind == CJ_OBJECT;
        str(b, object ? "{" : "[");
        size_t count = 0;
        for (size_t i = t + 1; i < token.next; i = object ? d->tokens[d->tokens[i].next].next : d->tokens[i].next) {
            if (count++) str(b, ",");
            if (pretty) { str(b, "\n"); spaces(b, (depth + 1) * 2); }
            if (object) { raw(b, d, i, pretty, depth + 1); str(b, pretty ? ": " : ":"); raw(b, d, d->tokens[i].next, pretty, depth + 1); }
            else raw(b, d, i, pretty, depth + 1);
        }
        if (pretty && count) { str(b, "\n"); spaces(b, depth * 2); }
        str(b, object ? "}" : "]");
    } else if (token.kind == CJ_NUMBER) {
        size_t n = token.end - token.begin;
        char number[1024];
        if (n >= sizeof(number)) { b->failed = 1; return; }
        memcpy(number, d->bytes + token.begin, n); number[n] = 0;
        double v = strtod(number, NULL);
        if (!isfinite(v)) str(b, "null");
        else if (v == 0) str(b, "0");
        else put(b, number, n);
    } else put(b, (const char *)d->bytes + token.begin, token.end - token.begin);
}
static void optional(buffer *b, cj_document *d, size_t t, cj_kind kind)
{
    if (is_kind(d, t, kind)) raw(b, d, t, 0, 0); else str(b, "null");
}
static int digits(const char *s, size_t start, unsigned count)
{
    int n = 0;
    for (unsigned i = 0; i < count; ++i) { unsigned char c = (unsigned char)s[start + i]; if (c < '0' || c > '9') return -1; n = n * 10 + c - '0'; }
    return n;
}
static void timestamp(buffer *out, cj_document *d, size_t t)
{
    if (t == CJ_MISSING || is_kind(d, t, CJ_NULL)) { str(out, "null"); return; }
    if (is_kind(d, t, CJ_NUMBER)) { raw(out, d, t, 0, 0); return; }
    char *s = cstring(d, t);
    if (!s) { out->failed = 1; return; }
    size_t length = strlen(s); int ok = length >= 10 && s[4] == '-' && s[7] == '-';
    int y = ok ? digits(s, 0, 4) : -1, m = ok ? digits(s, 5, 2) : -1, day = ok ? digits(s, 8, 2) : -1;
    int h = 0, minute = 0, second = 0, millis = 0, offset = 0;
    size_t p = 10;
    if (ok && length > 10) {
        ok = length >= 20 && (s[10] == 'T' || s[10] == 't') && s[13] == ':' && s[16] == ':';
        if (ok) { h = digits(s, 11, 2); minute = digits(s, 14, 2); second = digits(s, 17, 2); p = 19; }
        if (ok && s[p] == '.') {
            ++p; size_t start = p; unsigned places = 0;
            while (p < length && s[p] >= '0' && s[p] <= '9') { if (places < 3) { millis = millis * 10 + s[p] - '0'; ++places; } ++p; }
            ok = p > start; while (places < 3) { millis *= 10; ++places; }
        }
        if (ok && (s[p] == 'Z' || s[p] == 'z')) ++p;
        else if (ok && p + 6 == length && (s[p] == '+' || s[p] == '-') && s[p + 3] == ':') {
            int hours = digits(s, p + 1, 2), minutes = digits(s, p + 4, 2);
            ok = hours >= 0 && hours <= 23 && minutes >= 0 && minutes <= 59;
            offset = (hours * 60 + minutes) * (s[p] == '+' ? 1 : -1); p += 6;
        } else ok = 0;
    }
    int leap = y >= 0 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
    static const int lengths[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    ok = ok && p == length && y >= 1 && m >= 1 && m <= 12 && day >= 1 && day <= lengths[m - 1] + (m == 2 && leap)
        && h >= 0 && h <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
    if (ok) {
        long long before = y - 1;
        long long days = (long long)(y - 1970) * 365 + before / 4 - before / 100 + before / 400 - (1969 / 4 - 1969 / 100 + 1969 / 400);
        for (int month = 1; month < m; ++month) days += lengths[month - 1] + (month == 2 && leap);
        days += day - 1;
        long long seconds = days * 86400 + h * 3600 + minute * 60 + second - offset * 60;
        char value[64]; (void)snprintf(value, sizeof(value), "%.3f", (double)seconds + (double)millis / 1000.0); str(out, value);
    } else out->failed = 1;
    free(s);
}
static void parsed_message(buffer *b, cj_document *d, size_t t)
{
    if (!is_kind(d, t, CJ_OBJECT)) { raw(b, d, t, 0, 0); return; }
    str(b, "{"); unsigned fields = 0;
    for (size_t k = t + 1; k < d->tokens[t].next; k = d->tokens[d->tokens[k].next].next) {
        if (fields++) str(b, ",");
        raw(b, d, k, 0, 0); str(b, ":"); char key[257];
        if (cj_text(d, k, key, sizeof(key)) && (!strcmp(key, "create_time") || !strcmp(key, "update_time"))) timestamp(b, d, d->tokens[k].next);
        else raw(b, d, d->tokens[k].next, 0, 0);
    }
    if (member(d, t, "create_time") == CJ_MISSING) { if (fields) str(b, ","); str(b, "\"create_time\":null"); }
    str(b, "}");
}
static void parsed_node(buffer *b, cj_document *d, size_t t)
{
    str(b, "{"); unsigned fields = 0;
    for (size_t k = t + 1; k < d->tokens[t].next; k = d->tokens[d->tokens[k].next].next) {
        if (fields++) str(b, ",");
        raw(b, d, k, 0, 0); str(b, ":");
        char key[257];
        if (cj_text(d, k, key, sizeof(key)) && !strcmp(key, "message")) parsed_message(b, d, d->tokens[k].next);
        else raw(b, d, d->tokens[k].next, 0, 0);
    }
    str(b, "}");
}
static void escaped(buffer *b, const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (s[i] == '&') str(b, "&amp;"); else if (s[i] == '<') str(b, "&lt;");
        else if (s[i] == '>') str(b, "&gt;"); else put(b, s + i, 1);
    }
}
static void finding(normalizer *v, const char *code, const char *message, const char *node_id, const char *message_id)
{
    if (v->finding_count++) str(&v->findings, ",");
    str(&v->findings, "{\"severity\":\"warning\",\"code\":"); quote(&v->findings, code);
    str(&v->findings, ",\"message\":"); quote(&v->findings, message);
    str(&v->findings, ",\"nodeId\":"); quote(&v->findings, node_id);
    str(&v->findings, ",\"messageId\":"); quote(&v->findings, message_id); str(&v->findings, "}");
}
static size_t first_string(cj_document *d, size_t object, const char *a, const char *b, const char *c)
{
    size_t t = member(d, object, a);
    if (!is_kind(d, t, CJ_STRING) && b) t = member(d, object, b);
    if (!is_kind(d, t, CJ_STRING) && c) t = member(d, object, c);
    return is_kind(d, t, CJ_STRING) ? t : CJ_MISSING;
}
static buffer content_text(cj_document *d, size_t content)
{
    const char *keys[] = { "text", "code", "result", "output" };
    buffer out = {0};
    for (size_t i = 0; i < 4; ++i) {
        size_t t = member(d, content, keys[i]), n; char *s = text(d, t, &n);
        if (s) { put(&out, s, n); free(s); return out; }
    }
    size_t parts = member(d, content, "parts"); unsigned count = 0;
    if (is_kind(d, parts, CJ_ARRAY)) for (size_t i = parts + 1; i < d->tokens[parts].next; i = d->tokens[i].next) {
        size_t n; char *s = text(d, i, &n);
        if (s) { if (count++) str(&out, "\n"); put(&out, s, n); free(s); }
    }
    return out;
}
/* Accept HTTP(S) references only. The raw URL remains in raw evidence even
 * when it cannot be represented safely as a Markdown link. */
static char *http_url(const char *s)
{
    if (!s) return NULL;
    size_t scheme = !strncmp(s, "https://", 8) ? 8 : !strncmp(s, "http://", 7) ? 7 : 0;
    if (!scheme || !s[scheme]) return NULL;
    for (size_t i = 0; s[i]; ++i) if ((unsigned char)s[i] <= 32 || s[i] == '\\' || s[i] == '<' || s[i] == '>') return NULL;
    size_t end = scheme; while (s[end] && s[end] != '/' && s[end] != '?' && s[end] != '#') ++end;
    if (end == scheme) return NULL;
    buffer out = {0}; put(&out, s, scheme);
    for (size_t i = scheme; i < end; ++i) { char c = (char)tolower((unsigned char)s[i]); put(&out, &c, 1); }
    if (s[end] != '/') str(&out, "/");
    str(&out, s + end);
    return out.failed ? (free(out.p), (char *)NULL) : out.p;
}
typedef struct {
    const char *kind, *value;
    size_t value_bytes, language, title, asset;
    const char *url;
    size_t raw;
} part;
static part new_part(const char *kind, size_t raw_token)
{
    part p = { kind, NULL, 0, CJ_MISSING, CJ_MISSING, CJ_MISSING, NULL, raw_token }; return p;
}
static void emit_part(normalizer *v, buffer *json, buffer *md, part p, unsigned *count)
{
    cj_document *d = &v->d;
    if ((*count)++) str(json, ",");
    str(json, "{\"kind\":"); quote(json, p.kind);
    if (p.value) { str(json, ",\"text\":"); quoted(json, p.value, p.value_bytes); }
    if (p.language != CJ_MISSING) { str(json, ",\"language\":"); raw(json, d, p.language, 0, 0); }
    if (p.title != CJ_MISSING) { str(json, ",\"title\":"); raw(json, d, p.title, 0, 0); }
    if (p.asset != CJ_MISSING) { str(json, ",\"assetId\":"); raw(json, d, p.asset, 0, 0); }
    if (p.url) { str(json, ",\"url\":"); quote(json, p.url); }
    str(json, ",\"raw\":"); raw(json, d, p.raw, 0, 0); str(json, "}");
    const char *value = p.value ? p.value : "";
    if (!strcmp(p.kind, "text")) { escaped(md, value, p.value_bytes); str(md, "\n\n"); }
    else if (!strcmp(p.kind, "code")) {
        size_t longest = 0, run = 0;
        for (size_t i = 0; i < p.value_bytes; ++i) { run = value[i] == '`' ? run + 1 : 0; if (run > longest) longest = run; }
        size_t fence = longest < 3 ? 3 : longest + 1;
        for (size_t i = 0; i < fence; ++i) str(md, "`");
        size_t n; char *language = text(d, p.language, &n);
        if (language) { put(md, language, n); free(language); }
        str(md, "\n"); put(md, value, p.value_bytes); str(md, "\n");
        for (size_t i = 0; i < fence; ++i) str(md, "`");
        str(md, "\n\n");
    } else if (!strcmp(p.kind, "asset")) {
        str(md, "[Asset: "); size_t n; char *asset = text(d, p.asset, &n);
        if (asset) { escaped(md, asset, n); free(asset); } else str(md, "unresolved"); str(md, "]\n\n");
    } else if (!strcmp(p.kind, "citation")) {
        size_t n = 0; char *title = text(d, p.title, &n);
        const char *label = title ? title : p.value ? p.value : p.url ? p.url : "Citation";
        size_t bytes = title ? n : p.value ? p.value_bytes : strlen(label);
        if (p.url) str(md, "[");
        escaped(md, label, bytes);
        if (p.url) { str(md, "]("); str(md, p.url); str(md, ")"); }
        str(md, "\n\n"); free(title);
    } else if (!strcmp(p.kind, "unknown")) {
        str(md, "```json\n"); raw(md, d, p.raw, 1, 0); str(md, "\n```\n\n");
    } else {
        str(md, "**");
        for (const char *k = p.kind; *k; ++k) { if (*k == '_') str(md, " "); else put(md, k, 1); }
        str(md, "**\n\n"); escaped(md, value, p.value_bytes); str(md, "\n\n");
    }
}
static void citation(normalizer *v, buffer *json, buffer *md, size_t reference, unsigned *count, const char *node_id, const char *message_id)
{
    cj_document *d = &v->d; part p = new_part("citation", reference);
    p.title = first_string(d, reference, "title", NULL, NULL);
    char *value = text(d, member(d, reference, "text"), &p.value_bytes); p.value = value;
    char *url = cstring(d, first_string(d, reference, "url", "href", NULL));
    char *safe = http_url(url); p.url = safe;
    if (url && url[0] && !safe) finding(v, "UNSAFE_CITATION_URL", "A citation used a non-HTTP(S) URL.", node_id, message_id);
    emit_part(v, json, md, p, count); free(value); free(url); free(safe);
}
static void normalize_part(normalizer *v, buffer *json, buffer *md, size_t t, unsigned *count, const char *node_id, const char *message_id)
{
    cj_document *d = &v->d; part p = new_part("unknown", t); char *value = NULL;
    if (is_kind(d, t, CJ_STRING)) { p.kind = "text"; value = text(d, t, &p.value_bytes); p.value = value; }
    else if (!is_kind(d, t, CJ_OBJECT)) finding(v, "UNKNOWN_CONTENT_PART", "A content part had an unsupported primitive shape.", node_id, message_id);
    else {
        char *type = cstring(d, first_string(d, t, "content_type", "type", NULL));
        if (!type) { type = malloc(8); if (type) memcpy(type, "unknown", 8); }
        if (!type) { json->failed = 1; return; }
        if (!strcmp(type, "text") || !strcmp(type, "code")) {
            p.kind = !strcmp(type, "text") ? "text" : "code";
            value = text(d, first_string(d, t, "text", !strcmp(type, "code") ? "code" : NULL, NULL), &p.value_bytes);
            p.value = value ? value : ""; p.language = !strcmp(type, "code") ? first_string(d, t, "language", NULL, NULL) : CJ_MISSING;
        } else if (!strcmp(type, "image_asset_pointer") || !strcmp(type, "audio_asset_pointer") || !strcmp(type, "video_asset_pointer") || !strcmp(type, "file") || !strcmp(type, "file_pointer")) {
            p.kind = "asset"; p.asset = first_string(d, t, "asset_pointer", "file_id", "id");
        } else if (strstr(type, "citation") || !strcmp(type, "content_reference")) {
            citation(v, json, md, t, count, node_id, message_id); free(type); return;
        } else if (strstr(type, "tool_call") || strstr(type, "tool_result") || strstr(type, "browsing")) {
            p.kind = strstr(type, "tool_call") ? "tool_call" : "tool_result";
            value = text(d, member(d, t, "text"), &p.value_bytes);
            if (!value) { buffer compact = {0}; raw(&compact, d, t, 0, 0); value = compact.p; p.value_bytes = compact.n; if (compact.failed) json->failed = 1; }
            p.value = value;
        } else {
            buffer why = {0}; str(&why, "Unknown content part type "); str(&why, type); str(&why, ".");
            if (!why.failed) finding(v, "UNKNOWN_CONTENT_PART", why.p, node_id, message_id); else json->failed = 1; free(why.p);
        }
        free(type);
    }
    emit_part(v, json, md, p, count); free(value);
}
static void parts(normalizer *v, buffer *json, buffer *md, size_t message, const char *node_id, const char *message_id)
{
    cj_document *d = &v->d; size_t content = member(d, message, "content"), metadata = member(d, message, "metadata");
    char *type = cstring(d, member(d, content, "content_type")); unsigned count = 0; str(json, "[");
    if (!type) { json->failed = 1; return; }
    size_t raw_parts = member(d, content, "parts");
    if (!strcmp(type, "text") || !strcmp(type, "multimodal_text")) {
        if (is_kind(d, raw_parts, CJ_ARRAY)) for (size_t t = raw_parts + 1; t < d->tokens[raw_parts].next; t = d->tokens[t].next)
            normalize_part(v, json, md, t, &count, node_id, message_id);
    } else {
        const char *kind = "unknown";
        if (!strcmp(type, "code")) kind = "code";
        else if (!strcmp(type, "execution_output")) kind = "execution_output";
        else if (!strcmp(type, "tether_browsing_display") || !strcmp(type, "browsing_result") || !strcmp(type, "tool_result")) kind = "tool_result";
        else if (!strcmp(type, "thoughts") || !strcmp(type, "reasoning_recap") || !strcmp(type, "reasoning_summary")) kind = "reasoning_summary";
        else if (!strcmp(type, "canvas") || !strcmp(type, "textdoc") || !strcmp(type, "code_edit")) kind = "canvas";
        else if (!strcmp(type, "tool_call") || !strcmp(type, "computer_initialize_state")) kind = "tool_call";
        buffer value = {0}; part p = new_part(kind, content);
        if (strcmp(kind, "unknown")) { value = content_text(d, content); p.value = value.p ? value.p : ""; p.value_bytes = value.n; if (value.failed) json->failed = 1; }
        else { buffer why = {0}; str(&why, "Unknown content type "); str(&why, type); str(&why, "."); if (!why.failed) finding(v, "UNKNOWN_CONTENT_TYPE", why.p, node_id, message_id); else json->failed = 1; free(why.p); }
        if (!strcmp(kind, "code")) p.language = first_string(d, content, "language", NULL, NULL);
        emit_part(v, json, md, p, &count); free(value.p);
    }
    const char *keys[] = { "content_references", "citations", "sources" };
    for (unsigned k = 0; k < 3; ++k) {
        size_t a = member(d, metadata, keys[k]);
        if (is_kind(d, a, CJ_ARRAY)) for (size_t t = a + 1; t < d->tokens[a].next; t = d->tokens[t].next)
            if (is_kind(d, t, CJ_OBJECT)) citation(v, json, md, t, &count, node_id, message_id);
    }
    if (is_kind(d, member(d, metadata, "is_async_task_result_message"), CJ_TRUE) || is_kind(d, member(d, metadata, "deep_research_version"), CJ_STRING)) {
        buffer value = content_text(d, content); part p = new_part("deep_research", metadata);
        /* This title is constant rather than a provider JSON token. */
        p.value = value.p ? value.p : ""; p.value_bytes = value.n;
        size_t start = json->n; emit_part(v, json, md, p, &count);
        if (!json->failed && json->n > start) { --json->n; str(json, ",\"title\":\"Deep research result\"}"); }
        if (value.failed) json->failed = 1;
        free(value.p);
    }
    if (!count) { part p = new_part("text", content); p.value = ""; emit_part(v, json, md, p, &count); }
    str(json, "]"); free(type);
}
/* Provider graph identifiers are bounded ASCII. Match the reference's ASCII
 * locale ordering: punctuation, digits, case-folded letters, then lowercase. */
static int id_weight(unsigned char c) { return c == '_' ? 0 : c == '-' ? 1 : isdigit(c) ? c - '0' + 2 : tolower(c) - 'a' + 12; }
static int compare_nodes(const void *left, const void *right)
{
    const node *a = left, *b = right; size_t i = 0;
    for (; a->id[i] && b->id[i]; ++i) { int n = id_weight((unsigned char)a->id[i]) - id_weight((unsigned char)b->id[i]); if (n) return n; }
    if (a->id[i] || b->id[i]) return a->id[i] ? 1 : -1;
    for (i = 0; a->id[i]; ++i) if (a->id[i] != b->id[i]) return islower((unsigned char)a->id[i]) ? -1 : 1;
    return 0;
}
static int index_of(node *nodes, unsigned count, const char *id)
{
    for (unsigned i = 0; i < count; ++i) if (!strcmp(nodes[i].id, id)) return (int)i;
    return -1;
}
static void fallback(buffer *out, cj_document *d, cj_document *listing, const char *name, cj_kind kind)
{
    size_t a = member(d, 0, name);
    if (kind == CJ_NUMBER) {
        if (a != CJ_MISSING && !is_kind(d, a, CJ_NULL)) timestamp(out, d, a);
        else timestamp(out, listing, member(listing, 0, name));
    } else if (is_kind(d, a, kind)) optional(out, d, a, kind);
    else optional(out, listing, member(listing, 0, name), kind);
}
void cgw_views_free(cgw_views *views)
{
    if (!views) return;
    free(views->json); free(views->markdown); memset(views, 0, sizeof(*views));
}
int cgw_views_normalize(const unsigned char *detail, size_t detail_bytes, const unsigned char *listing_bytes, size_t listing_length,
                        const char *workspace, unsigned archived, size_t ceiling, cgw_views *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!workspace || !workspace[0] || strlen(workspace) > 256 || archived > 1 || !ceiling) return 0;
    normalizer v = {0}; cj_document listing;
    if (!cj_parse(&v.d, detail, detail_bytes, ceiling)) return 0;
    if (!cj_parse(&listing, listing_bytes, listing_length, ceiling)) { cj_free(&v.d); return 0; }
    cj_document *d = &v.d; buffer json = {0}, md = {0}; node *nodes = calloc(NODE_LIMIT, sizeof(*nodes));
    unsigned count = 0; int ok = nodes != NULL;
    char *id = cstring(d, first_string(d, 0, "conversation_id", "id", NULL));
    char *listed = cstring(&listing, member(&listing, 0, "id"));
    if (!id || !listed || strcmp(id, listed)) ok = 0;
    size_t mapping = member(d, 0, "mapping");
    if (!is_kind(d, mapping, CJ_OBJECT)) ok = 0;
    for (size_t k = ok ? mapping + 1 : d->count; ok && k < d->tokens[mapping].next; k = d->tokens[d->tokens[k].next].next) {
        if (count == NODE_LIMIT) { ok = 0; break; }
        node *n = &nodes[count++]; n->node = d->tokens[k].next; n->message = member(d, n->node, "message");
        if (!cj_text(d, k, n->id, sizeof(n->id))) { ok = 0; break; }
        size_t parent = member(d, n->node, "parent");
        if (!is_kind(d, parent, CJ_NULL) && !cj_text(d, parent, n->parent, sizeof(n->parent))) ok = 0;
    }
    if (ok) qsort(nodes, count, sizeof(*nodes), compare_nodes);
    char *current = cstring(d, member(d, 0, "current_node")); int selected[NODE_LIMIT]; unsigned selected_count = 0;
    int position = current && ok ? index_of(nodes, count, current) : -1;
    while (position >= 0 && !nodes[position].selected && selected_count < count) {
        nodes[position].selected = 1; selected[selected_count++] = position;
        position = nodes[position].parent[0] ? index_of(nodes, count, nodes[position].parent) : -1;
    }
    if (!selected_count || position >= 0) ok = 0;
    unsigned char digest[32]; char workspace_hex[65]; cgw_sha256 hash; cgw_sha256_init(&hash);
    static const char prefix[] = "chatgpt-web-workspace-v1";
    if (!cgw_sha256_update(&hash, prefix, sizeof(prefix)) || !cgw_sha256_update(&hash, workspace, strlen(workspace)) || !cgw_sha256_final(&hash, digest)) ok = 0;
    cgw_sha256_hex(digest, workspace_hex); workspace_hex[32] = 0;
    if (ok) {
        str(&json, "{\"schemaVersion\":1,\"normalizerVersion\":\"chatgpt-native-c1\",\"provider\":\"chatgpt-web\",\"logicalKey\":");
        buffer key = {0}; str(&key, workspace_hex); str(&key, "/"); str(&key, id);
        if (key.failed) json.failed = 1; else quote(&json, key.p);
        free(key.p);
        str(&json, ",\"conversationId\":"); quote(&json, id); str(&json, ",\"workspaceFingerprint\":"); quote(&json, workspace_hex);
        str(&json, ",\"title\":"); fallback(&json, d, &listing, "title", CJ_STRING);
        str(&json, ",\"createTime\":"); fallback(&json, d, &listing, "create_time", CJ_NUMBER);
        str(&json, ",\"updateTime\":"); fallback(&json, d, &listing, "update_time", CJ_NUMBER);
        str(&json, ",\"currentNodeId\":"); quote(&json, current); str(&json, ",\"rootNodeIds\":[");
        unsigned roots = 0;
        for (unsigned i = 0; i < count; ++i) if (!nodes[i].parent[0]) { if (roots++) str(&json, ","); quote(&json, nodes[i].id); }
        str(&json, archived ? "],\"memberships\":[{\"scope\":\"archived\"}],\"nodes\":[" : "],\"memberships\":[{\"scope\":\"main\"}],\"nodes\":[");
        for (unsigned i = 0; i < count; ++i) {
            node *n = &nodes[i]; if (i) str(&json, ","); str(&json, "{\"id\":"); quote(&json, n->id);
            str(&json, ",\"messageId\":"); optional(&json, d, member(d, n->message, "id"), CJ_STRING);
            str(&json, ",\"parentId\":"); if (n->parent[0]) quote(&json, n->parent); else str(&json, "null");
            str(&json, ",\"childIds\":"); raw(&json, d, member(d, n->node, "children"), 0, 0);
            str(&json, ",\"raw\":"); parsed_node(&json, d, n->node); str(&json, "}");
        }
        str(&json, "],\"messages\":["); unsigned messages = 0;
        for (unsigned i = 0; i < count; ++i) {
            node *n = &nodes[i]; if (!is_kind(d, n->message, CJ_OBJECT)) continue;
            size_t m = n->message, author = member(d, m, "author"), metadata = member(d, m, "metadata");
            char *message_id = cstring(d, member(d, m, "id")), *original_role = cstring(d, member(d, author, "role"));
            if (!message_id || !original_role) { free(message_id); free(original_role); ok = 0; break; }
            const char *role = (!strcmp(original_role, "user") || !strcmp(original_role, "assistant") || !strcmp(original_role, "tool") || !strcmp(original_role, "system")) ? original_role : "unknown";
            if (!strcmp(role, "unknown")) { buffer why = {0}; str(&why, "Unknown provider role "); str(&why, original_role); str(&why, "."); if (why.failed) ok = 0; else finding(&v, "UNKNOWN_ROLE", why.p, n->id, message_id); free(why.p); }
            if (messages++) str(&json, ",");
            str(&json, "{\"id\":"); quote(&json, message_id); str(&json, ",\"nodeId\":"); quote(&json, n->id);
            str(&json, ",\"role\":"); quote(&json, role); str(&json, ",\"authorName\":"); optional(&json, d, member(d, author, "name"), CJ_STRING);
            str(&json, ",\"parentId\":"); if (n->parent[0]) quote(&json, n->parent); else str(&json, "null");
            str(&json, ",\"childIds\":"); raw(&json, d, member(d, n->node, "children"), 0, 0);
            str(&json, ",\"createTime\":"); timestamp(&json, d, member(d, m, "create_time"));
            str(&json, ",\"updateTime\":"); timestamp(&json, d, member(d, m, "update_time"));
            str(&json, ",\"recipient\":"); optional(&json, d, member(d, m, "recipient"), CJ_STRING);
            str(&json, ",\"modelSlug\":"); optional(&json, d, first_string(d, metadata, "model_slug", "model", NULL), CJ_STRING);
            str(&json, ",\"status\":"); optional(&json, d, member(d, m, "status"), CJ_STRING);
            str(&json, ",\"endTurn\":"); size_t end = member(d, m, "end_turn");
            if (is_kind(d, end, CJ_TRUE) || is_kind(d, end, CJ_FALSE)) raw(&json, d, end, 0, 0); else str(&json, "null");
            str(&json, n->selected ? ",\"selected\":true,\"parts\":" : ",\"selected\":false,\"parts\":");
            str(&n->md, "### "); str(&n->md, role);
            size_t name_length; char *name = text(d, member(d, author, "name"), &name_length);
            if (name && name_length) { str(&n->md, " · "); escaped(&n->md, name, name_length); } free(name);
            str(&n->md, " {#message-"); for (const char *p = message_id; *p; ++p) { char c = (char)tolower((unsigned char)*p); put(&n->md, &c, 1); }
            str(&n->md, "}\n\n<!-- node:"); escaped(&n->md, n->id, strlen(n->id)); str(&n->md, " -->\n\n");
            parts(&v, &json, &n->md, m, n->id, message_id); str(&n->md, "\n");
            str(&json, ",\"metadata\":"); if (metadata == CJ_MISSING || is_kind(d, metadata, CJ_NULL)) str(&json, "{}"); else raw(&json, d, metadata, 0, 0);
            str(&json, ",\"extensions\":{\"chatgpt\":"); parsed_message(&json, d, m); str(&json, "}}"); free(message_id); free(original_role);
        }
        str(&json, "],\"findings\":["); put(&json, v.findings.p ? v.findings.p : "", v.findings.n);
        str(&json, "],\"coverage\":{\"messages\":\"returned_provider_graph\",\"providerGraph\":\"unknown\",\"versions\":\"unknown\"},\"extensions\":{\"chatgpt\":{");
        unsigned extensions = 0;
        for (size_t k = 1; k < d->tokens[0].next; k = d->tokens[d->tokens[k].next].next) {
            char *key = cstring(d, k); if (!key) { ok = 0; break; }
            if (strcmp(key, "mapping") && strcmp(key, "__pagination_evidence")) {
                if (extensions++) str(&json, ",");
                raw(&json, d, k, 0, 0); str(&json, ":");
                if (!strcmp(key, "create_time") || !strcmp(key, "update_time")) timestamp(&json, d, d->tokens[k].next);
                else raw(&json, d, d->tokens[k].next, 0, 0);
            }
            free(key);
        }
        const char *defaulted[] = { "title", "create_time", "update_time", "current_node" };
        for (unsigned i = 0; i < 4; ++i) if (member(d, 0, defaulted[i]) == CJ_MISSING) { if (extensions++) str(&json, ","); quote(&json, defaulted[i]); str(&json, ":null"); }
        str(&json, "}}}\n");
        str(&md, "# ");
        size_t title = member(d, 0, "title"), title_length; char *title_text = text(d, title, &title_length);
        if (!title_text) title_text = text(&listing, member(&listing, 0, "title"), &title_length);
        if (title_text) { escaped(&md, title_text, title_length); free(title_text); } else str(&md, "Untitled conversation");
        str(&md, "\n\n- Provider: ChatGPT Web\n- Conversation ID: `"); str(&md, id); str(&md, "`\n- Workspace: `"); str(&md, workspace_hex); str(&md, "`\n\n## Selected branch\n\n");
        for (unsigned i = selected_count; i > 0; --i) { buffer *a = &nodes[selected[i - 1]].md; put(&md, a->p ? a->p : "", a->n); }
        unsigned alternatives = 0;
        for (unsigned i = 0; i < count; ++i) if (!nodes[i].selected && nodes[i].md.n) { if (!alternatives++) str(&md, "## Alternative branches\n\n"); put(&md, nodes[i].md.p, nodes[i].md.n); }
        if (v.finding_count) {
            str(&md, "## Validation findings\n\n"); buffer f = {0}; str(&f, "["); put(&f, v.findings.p, v.findings.n); str(&f, "]"); cj_document findings;
            if (!f.failed && cj_parse(&findings, (const unsigned char *)f.p, f.n, ceiling)) {
                for (size_t i = 1; i < findings.tokens[0].next; i = findings.tokens[i].next) {
                    size_t n; char *code = text(&findings, member(&findings, i, "code"), &n);
                    str(&md, "- **WARNING · "); if (code) { escaped(&md, code, n); free(code); }
                    str(&md, ":** "); char *why = text(&findings, member(&findings, i, "message"), &n);
                    if (why) { escaped(&md, why, n); free(why); } str(&md, "\n");
                }
                cj_free(&findings);
            } else ok = 0;
            free(f.p);
        }
        while (md.n && isspace((unsigned char)md.p[md.n - 1])) --md.n;
        str(&md, "\n");
    }
    for (unsigned i = 0; i < count; ++i) { if (nodes[i].md.failed) ok = 0; free(nodes[i].md.p); }
    free(nodes); free(current); free(id); free(listed); free(v.findings.p);
    if (d->work > ceiling || listing.work > ceiling || json.failed || md.failed || v.findings.failed) ok = 0;
    cj_free(d); cj_free(&listing);
    if (!ok) { free(json.p); free(md.p); return 0; }
    out->json = json.p; out->json_bytes = json.n; out->markdown = md.p; out->markdown_bytes = md.n; return 1;
}
