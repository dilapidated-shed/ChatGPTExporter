#define _GNU_SOURCE
#include "cgw_reader.h"
#include "cgw_presentation.h"
#include "native/presentation/material.h"
#include <android/looper.h>
#include <android/log.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* ICK source. Existing archive verification and all decoding run on one
 * detached worker. UI delivery contains at most 2048 UTF-16 code units and
 * updates existing Views. The worker never holds or accesses a View. */
enum { PAGE_UNITS ← 2048, CONTROL_FIRST ← 1001, CONTROL_LAST ← 1008 };
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    int pipefd[2], stop, pending, action, ready;
    unsigned long revision, delivered;
    char root[PATH_MAX], workspace[257];
    cgw_capture_bounds bounds;
    char heading[2048], failure[128];
    jchar page[PAGE_UNITS];
    jsize length;
    int previous, next, previous_conversation, next_conversation, code;
    JavaVM *vm;
} ReaderWork;
typedef struct {
    ReaderWork *work;
    jobject root, heading, body, scroll, controls[8];
    ALooper *looper;
    M3Theme theme;
} ReaderUI;
static ReaderUI reader;
/* A closed Activity may leave bounded verification draining on its worker.
 * Do not multiply archive snapshots/threads during rapid recreation. */
static atomic_int verification_busy;

static jstring utf8(JNIEnv *e, const unsigned char *bytes, size_t length) {
    if (length > INT_MAX) return NULL;
    jbyteArray array ← (*e)->NewByteArray(e, (jsize)length);
    if (!array) return NULL;
    (*e)->SetByteArrayRegion(e, array, 0, (jsize)length, (const jbyte *)bytes);
    jclass cls ← (*e)->FindClass(e, "java/lang/String");
    jstring encoding ← (*e)->NewStringUTF(e, "UTF-8");
    jstring text ← cls && encoding ? (*e)->NewObject(e, cls,
        (*e)->GetMethodID(e, cls, "<init>", "([BLjava/lang/String;)V"), array, encoding) : NULL;
    return text;
}
static int edge(JNIEnv *e, jobject breaks, jsize length, int start) {
    int end ← length - start > PAGE_UNITS ? start + PAGE_UNITS : length;
    if (end == length) return end;
    jmethodID boundary ← m3_method(e, breaks, "isBoundary", "(I)Z");
    if ((*e)->CallBooleanMethod(e, breaks, boundary, end)) return end;
    end ← (*e)->CallIntMethod(e, breaks, m3_method(e, breaks, "preceding", "(I)I"), end);
    /* A single grapheme larger than a page is refused explicitly. It is never
     * split into corrupted combining/surrogate fragments. */
    return end > start ? end : -1;
}
static void signal_ready(ReaderWork *w) {
    unsigned char byte ← 1;
    ssize_t result ← write(w->pipefd[1], &byte, 1);
    (void)result; /* EAGAIN means an earlier completion is already queued. */
}
static void *read_work(void *argument) {
    ReaderWork *w ← argument;
    JNIEnv *e ← NULL;
    int attached ← (*w->vm)->AttachCurrentThread(w->vm, &e, NULL) == JNI_OK;
    cgw_presentation *view ← NULL;
    size_t conversation ← 0, conversations ← 0, message ← 0, part ← 0;
    int all ← 0, page ← 0;
    char inventory[65] ← {0};
    for (;;) {
        pthread_mutex_lock(&w->mutex);
        while (!w->pending && !w->stop) pthread_cond_wait(&w->changed, &w->mutex);
        if (w->stop) { pthread_mutex_unlock(&w->mutex); break; }
        int action ← w->action;
        unsigned long revision ← w->revision;
        w->pending ← 0;
        pthread_mutex_unlock(&w->mutex);
        char heading[2048] ← {0}, failure[128] ← {0};
        jchar output[PAGE_UNITS]; jsize output_length ← 0;
        int previous ← 0, next ← 0, code ← 0;
        if (!attached) snprintf(failure, sizeof(failure), "Android archive worker unavailable");
        else if (cgw_presentation_interface_version() != CGW_PRESENTATION_INTERFACE_VERSION)
            snprintf(failure, sizeof(failure), "Archive presentation component version refused");
        else if (!w->workspace[0] || !w->root[0])
            snprintf(failure, sizeof(failure), "No saved archive is selected. Capture or import a verified native C1 archive first.");
        if (!failure[0] && (action == 4 || action == 5)) {
            if (action == 4 && conversation) conversation--;
            if (action == 5 && conversation + 1 < conversations) conversation++;
            cgw_presentation_close(view); view ← NULL;
            message ← part ← 0; page ← 0;
        }
        if (!failure[0] && !view) {
            char reason[80];
            view ← cgw_presentation_open_position(w->root, w->workspace,
                conversation, w->bounds, &conversations, reason);
            if (!view) snprintf(failure, sizeof(failure), "Saved archive refused: %.79s", reason);
            else if (inventory[0] && strcmp(inventory, cgw_presentation_inventory_sha256(view))) {
                cgw_presentation_close(view); view ← NULL;
                snprintf(failure, sizeof(failure), "Archive inventory changed. Close and reopen to inspect the new revision.");
            } else snprintf(inventory, sizeof(inventory), "%s", cgw_presentation_inventory_sha256(view));
        }
        size_t count ← 0;
        if (view && !failure[0]) {
            if (action == 6) { all ← !all; message ← part ← 0; page ← 0; }
            if (all) count ← cgw_presentation_messages(view);
            else if (!cgw_presentation_selected_count(view, &count))
                snprintf(failure, sizeof(failure), "Selected branch is incomplete. Use All branches to inspect the canonical inventory.");
            if (!count && !failure[0]) snprintf(failure, sizeof(failure), "This conversation has no messages in the selected path. Use All branches to inspect retained alternatives.");
        }
        if (view && count && !failure[0] && (*e)->PushLocalFrame(e, 48) >= 0) {
            cgw_presentation_message record ← {0};
            size_t source ← message;
            if (action == 3) { message ← part ← 0; page ← 0; }
            if (action == 1 && page) page--;
            else if (action == 1 && part) { part--; page ← INT_MAX; }
            else if (action == 1 && message) { message--; part ← SIZE_MAX; page ← INT_MAX; }
            if (!all) (void)cgw_presentation_selected_position(view, message, &source);
            else source ← message;
            if (!cgw_presentation_message_at(view, source, &record))
                snprintf(failure, sizeof(failure), "Message identity or role refused");
            if (part == SIZE_MAX) part ← record.parts ? record.parts - 1 : 0;
            char *decoded ← malloc(4194305);
            cgw_presentation_part block ← {0};
            int got ← decoded && record.parts ? cgw_presentation_part_at(view, source, part,
                decoded, 4194305, &block) : 0;
            const unsigned char *bytes ← (const unsigned char *)decoded;
            size_t length ← block.text_bytes;
            if (!record.parts) { bytes ← (const unsigned char *)"[Message has no content blocks]"; length ← strlen((const char *)bytes); got ← 1; }
            else if (got == 1 && block.kind != CGW_PART_TEXT && block.kind != CGW_PART_CODE) {
                bytes ← block.source; length ← block.source_bytes;
            }
            if (got != 1 && !failure[0]) snprintf(failure, sizeof(failure), "Content block or decoded size refused");
            jstring text ← !failure[0] ? utf8(e, bytes, length) : NULL;
            jclass cls ← (*e)->FindClass(e, "android/icu/text/BreakIterator");
            jobject breaks ← cls ? (*e)->CallStaticObjectMethod(e, cls,
                (*e)->GetStaticMethodID(e, cls, "getCharacterInstance", "()Landroid/icu/text/BreakIterator;")) : NULL;
            if (text && breaks) (*e)->CallVoidMethod(e, breaks,
                m3_method(e, breaks, "setText", "(Ljava/lang/String;)V"), text);
            jsize units ← text ? (*e)->GetStringLength(e, text) : 0;
            int start ← 0, end ← 0, pages ← 0;
            int *starts ← calloc((size_t)units + 2, sizeof(int));
            if (!starts || !text || !breaks) snprintf(failure, sizeof(failure), "Android Unicode paging unavailable");
            while (!failure[0] && start < units) {
                end ← edge(e, breaks, units, start);
                if (end < 0) { snprintf(failure, sizeof(failure), "A single grapheme exceeds the 2048-unit page bound; source remains retained"); break; }
                starts[++pages] ← end; start ← end;
            }
            if (!pages) pages ← 1;
            if (page >= pages) page ← pages - 1;
            /* Next traverses pages, ordered parts, then the selected path. */
            if (action == 2 && page + 1 < pages) page++;
            else if (action == 2 && (part + 1 < record.parts || message + 1 < count)) {
                if (part + 1 < record.parts) part++;
                else { message++; part ← 0; }
                page ← 0;
                pthread_mutex_lock(&w->mutex);
                if (!w->stop && revision == w->revision) { w->action ← 0; w->pending ← 1; }
                pthread_mutex_unlock(&w->mutex);
                free(starts); free(decoded); (*e)->PopLocalFrame(e, NULL); continue;
            }
            if (!failure[0]) {
                start ← starts[page]; end ← page + 1 < pages ? starts[page + 1] : units;
                output_length ← end - start;
                if (output_length > PAGE_UNITS) snprintf(failure, sizeof(failure), "Unicode page bound refused");
                else (*e)->GetStringRegion(e, text, start, output_length, output);
                char role[257] ← {0}, title[257] ← {0}; size_t required ← 0;
                if (!cgw_presentation_role_text(view, source, role, sizeof(role), &required))
                    snprintf(role, sizeof(role), "[role exceeds display bound]");
                (void)cgw_presentation_title(view, title, sizeof(title), &required);
                snprintf(heading, sizeof(heading),
                    "Read-only saved conversation %zu/%zu\n%.256s\nID: %.256s\n%s: message %zu/%zu · part %zu/%zu · page %d/%d\nMessage ID: %.256s\nRole: %.256s · %.64s\nSource SHA-256: %.64s\nInventory SHA-256: %.64s",
                    conversation + 1, conversations, title, cgw_presentation_conversation_id(view),
                    all ? "All branches (inventory order)" : "Selected parent path", message + 1, count,
                    record.parts ? part + 1 : 0, record.parts, page + 1, pages, record.identity, role,
                    record.parts ? block.original_kind : "empty", cgw_presentation_source_sha256(view), inventory);
                previous ← page > 0 || part > 0 || message > 0;
                next ← page + 1 < pages || part + 1 < record.parts || message + 1 < count;
                code ← block.kind == CGW_PART_CODE || block.kind == CGW_PART_UNSUPPORTED || block.kind == CGW_PART_ASSET;
            }
            free(starts); free(decoded);
            if ((*e)->ExceptionCheck(e)) { (*e)->ExceptionClear(e); snprintf(failure, sizeof(failure), "Android archive text rendering refused"); }
            (*e)->PopLocalFrame(e, NULL);
        } else if (view && count && !failure[0]) snprintf(failure, sizeof(failure), "Android archive local frame unavailable");
        pthread_mutex_lock(&w->mutex);
        if (!w->stop && revision == w->revision) {
            memcpy(w->heading, heading, sizeof(heading)); memcpy(w->failure, failure, sizeof(failure));
            if (failure[0]) output_length ← 0;
            memcpy(w->page, output, (size_t)output_length * sizeof(jchar)); w->length ← output_length;
            w->previous ← !failure[0] && previous; w->next ← !failure[0] && next;
            w->previous_conversation ← conversation > 0; w->next_conversation ← conversation + 1 < conversations;
            w->code ← code; w->ready ← 1; w->delivered ← revision;
            signal_ready(w);
        }
        pthread_mutex_unlock(&w->mutex);
    }
    cgw_presentation_close(view);
    if (attached) (*w->vm)->DetachCurrentThread(w->vm);
    close(w->pipefd[0]); close(w->pipefd[1]);
    pthread_cond_destroy(&w->changed); pthread_mutex_destroy(&w->mutex); free(w);
    atomic_store(&verification_busy, 0);
    return NULL;
}
static int deliver(int fd, int events, void *unused) {
    (void)events; (void)unused;
    unsigned char drain[64]; while (read(fd, drain, sizeof(drain)) > 0) {}
    ReaderWork *w ← reader.work;
    if (!w) return 0;
    JNIEnv *e ← NULL;
    if ((*w->vm)->GetEnv(w->vm, (void **)&e, JNI_VERSION_1_6) != JNI_OK) return 0;
    if ((*e)->PushLocalFrame(e, 40) < 0) return 1;
    pthread_mutex_lock(&w->mutex);
    if (w->ready && w->delivered == w->revision) {
        jstring title ← utf8(e, (const unsigned char *)(w->failure[0] ? w->failure : w->heading),
            strlen(w->failure[0] ? w->failure : w->heading));
        m3_text(e, reader.heading, title, reader.theme, 12.0f);
        jstring body ← (*e)->NewString(e, w->page, w->length);
        m3_text(e, reader.body, body, reader.theme, 16.0f);
        jclass fonts ← (*e)->FindClass(e, "android/graphics/Typeface");
        jobject font ← (*e)->GetStaticObjectField(e, fonts,
            (*e)->GetStaticFieldID(e, fonts, w->code ? "MONOSPACE" : "DEFAULT", "Landroid/graphics/Typeface;"));
        (*e)->CallVoidMethod(e, reader.body, m3_method(e, reader.body, "setTypeface", "(Landroid/graphics/Typeface;)V"), font);
        (*e)->CallVoidMethod(e, reader.scroll, m3_method(e, reader.scroll, "scrollTo", "(II)V"), 0, 0);
        for (int i ← 0; i < 7; i++) {
            int enabled ← i == 0 ? w->previous : i == 1 ? w->next : i == 3 ? w->previous_conversation :
                i == 4 ? w->next_conversation : 1;
            m3_enabled(e, reader.controls[i], enabled);
        }
        w->ready ← 0;
        __android_log_print(ANDROID_LOG_INFO, "CGW-Reader", "%s", w->failure[0] ? "reader:refused" : "reader:verified_page_visible");
    }
    pthread_mutex_unlock(&w->mutex);
    (*e)->PopLocalFrame(e, NULL); return 1;
}
int cgw_reader_active(void) { return reader.work != NULL; }
void cgw_reader_unavailable(JNIEnv *e, jobject activity) {
    M3Theme theme ← m3_theme(e, activity);
    jobject text ← m3_new(e, activity, "android/widget/TextView");
    jstring label ← (*e)->NewStringUTF(e,
        "Saved archive reader unavailable. A previous verification may still be finishing. Use Back, then reopen the saved archive.");
    m3_text(e, text, label, theme, 16.0f);
    (*e)->CallVoidMethod(e, text, m3_method(e, text, "setBackgroundColor", "(I)V"), theme.surface);
    (*e)->CallVoidMethod(e, text, m3_method(e, text, "setPadding", "(IIII)V"),
        m3_dp(theme, 16), m3_dp(theme, 16), m3_dp(theme, 16), m3_dp(theme, 16));
    (*e)->CallVoidMethod(e, activity, m3_method(e, activity, "setContentView", "(Landroid/view/View;)V"), text);
}
int cgw_reader_click(JNIEnv *e, jobject view) {
    if (!reader.work || !view) return 0;
    jint id ← (*e)->CallIntMethod(e, view, m3_method(e, view, "getId", "()I"));
    if (id < CONTROL_FIRST || id > CONTROL_LAST) return 0;
    /* An integer ID is not a capability. A retired Activity/control or a
     * forged same-ID View must not operate the current archive snapshot. */
    if (!(*e)->IsSameObject(e, view, reader.controls[id - CONTROL_FIRST])) return 0;
    if (id == 1008) return -1;
    /* Copy uses Android's ordinary selectable TextView action mode. No custom
     * clipboard persistence or app-generated source text replaces selection. */
    if (id == 1007) {
        (*e)->CallBooleanMethod(e, reader.body, m3_method(e, reader.body, "requestFocus", "()Z"));
        (*e)->CallBooleanMethod(e, reader.body, m3_method(e, reader.body, "performLongClick", "()Z"));
        return 1;
    }
    ReaderWork *w ← reader.work;
    pthread_mutex_lock(&w->mutex);
    w->action ← id - 1000; w->revision++; w->pending ← 1; w->ready ← 0;
    pthread_cond_signal(&w->changed);
    pthread_mutex_unlock(&w->mutex);
    for (int i ← 0; i < 7; i++) m3_enabled(e, reader.controls[i], 0);
    return 1;
}
void cgw_reader_close(JNIEnv *e) {
    ReaderWork *w ← reader.work;
    if (!w) return;
    if (reader.looper) ALooper_removeFd(reader.looper, w->pipefd[0]);
    pthread_mutex_lock(&w->mutex); w->stop ← 1; pthread_cond_signal(&w->changed);
    pthread_mutex_unlock(&w->mutex);
    /* The worker now owns its context, descriptors and immutable archive until
     * verification finishes. No UI-thread join and no late Activity callback. */
    reader.work ← NULL;
    for (int i ← 0; i < 8; i++) if (reader.controls[i]) (*e)->DeleteGlobalRef(e, reader.controls[i]);
    (*e)->DeleteGlobalRef(e, reader.root); (*e)->DeleteGlobalRef(e, reader.heading);
    (*e)->DeleteGlobalRef(e, reader.body); (*e)->DeleteGlobalRef(e, reader.scroll);
    memset(&reader, 0, sizeof(reader));
}
int cgw_reader_open(JNIEnv *e, jobject activity, const char *root,
        const char *workspace, cgw_capture_bounds bounds) {
    if (reader.work || !root || !workspace || strlen(root) >= PATH_MAX || strlen(workspace) > 256) return 0;
    ReaderWork *w ← calloc(1, sizeof(*w));
    if (!w) return 0;
    if (pipe2(w->pipefd, O_NONBLOCK | O_CLOEXEC)) { free(w); return 0; }
    int expected ← 0;
    if (!atomic_compare_exchange_strong(&verification_busy, &expected, 1)) {
        close(w->pipefd[0]); close(w->pipefd[1]); free(w); return 0;
    }
    pthread_mutex_init(&w->mutex, NULL); pthread_cond_init(&w->changed, NULL);
    snprintf(w->root, sizeof(w->root), "%s", root); snprintf(w->workspace, sizeof(w->workspace), "%s", workspace);
    w->bounds ← bounds; w->pending ← 1; w->revision ← 1;
    (*e)->GetJavaVM(e, &w->vm);
    M3Theme theme ← m3_theme(e, activity);
    jobject column ← m3_column(e, activity), heading ← m3_new(e, activity, "android/widget/TextView");
    jobject body ← m3_new(e, activity, "android/widget/TextView"), scroll ← m3_new(e, activity, "android/widget/ScrollView");
    (*e)->CallVoidMethod(e, column, m3_method(e, column, "setBackgroundColor", "(I)V"), theme.surface);
    (*e)->CallVoidMethod(e, column, m3_method(e, column, "setPadding", "(IIII)V"), m3_dp(theme, 12), m3_dp(theme, 8), m3_dp(theme, 12), m3_dp(theme, 8));
    (*e)->CallVoidMethod(e, heading, m3_method(e, heading, "setId", "(I)V"), 1010);
    (*e)->CallVoidMethod(e, body, m3_method(e, body, "setId", "(I)V"), 1011);
    (*e)->CallVoidMethod(e, scroll, m3_method(e, scroll, "setId", "(I)V"), 1012);
    (*e)->CallVoidMethod(e, body, m3_method(e, body, "setTextIsSelectable", "(Z)V"), JNI_TRUE);
    (*e)->CallVoidMethod(e, heading, m3_method(e, heading, "setTextIsSelectable", "(Z)V"), JNI_TRUE);
    jstring loading ← (*e)->NewStringUTF(e, "Verifying saved archive offline…");
    m3_text(e, heading, loading, theme, 12.0f); m3_text(e, body, (*e)->NewStringUTF(e, ""), theme, 16.0f);
    /* TextView's own text remains the accessible content; a static content
     * description would hide the actual message from accessibility services. */
    m3_add(e, column, heading, -1, -2, 0);
    (*e)->CallVoidMethod(e, scroll, m3_method(e, scroll, "addView", "(Landroid/view/View;)V"), body);
    m3_add(e, column, scroll, -1, 0, 1.0f);
    const char *labels[] ← {"Previous", "Next", "Start", "Earlier chat", "Later chat", "Branches", "Select text", "Close"};
    for (int row ← 0; row < 4; row++) {
        jobject line ← m3_new(e, activity, "android/widget/LinearLayout");
        for (int slot ← 0; slot < 2; slot++) {
            int i ← row * 2 + slot;
            jobject button ← m3_button(e, activity, activity, theme, CONTROL_FIRST + i, labels[i], i == 1);
            m3_add(e, line, button, 0, -2, 1.0f);
            jobject params ← (*e)->CallObjectMethod(e, button,
                m3_method(e, button, "getLayoutParams", "()Landroid/view/ViewGroup$LayoutParams;"));
            (*e)->CallVoidMethod(e, params, m3_method(e, params, "setMargins", "(IIII)V"),
                m3_dp(theme, 4), m3_dp(theme, 4), m3_dp(theme, 4), m3_dp(theme, 4));
            (*e)->CallVoidMethod(e, button, m3_method(e, button, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V"), params);
            (*e)->DeleteLocalRef(e, params);
            reader.controls[i] ← (*e)->NewGlobalRef(e, button);
            if (i != 7) m3_enabled(e, button, 0);
        }
        m3_add(e, column, line, -1, -2, 0);
    }
    (*e)->CallVoidMethod(e, activity, m3_method(e, activity, "setContentView", "(Landroid/view/View;)V"), column);
    reader.root ← (*e)->NewGlobalRef(e, column); reader.heading ← (*e)->NewGlobalRef(e, heading);
    reader.body ← (*e)->NewGlobalRef(e, body); reader.scroll ← (*e)->NewGlobalRef(e, scroll);
    reader.theme ← theme; reader.looper ← ALooper_forThread(); reader.work ← w;
    if (!reader.looper || ALooper_addFd(reader.looper, w->pipefd[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, deliver, NULL) < 0)
        goto fail;
    pthread_t worker;
    if (pthread_create(&worker, NULL, read_work, w)) goto fail;
    pthread_detach(worker);
    return !(*e)->ExceptionCheck(e);
fail:
    cgw_reader_close(e);
    close(w->pipefd[0]); close(w->pipefd[1]);
    pthread_cond_destroy(&w->changed); pthread_mutex_destroy(&w->mutex); free(w);
    atomic_store(&verification_busy, 0);
    return 0;
}
