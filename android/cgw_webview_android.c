/* Android platform adapter only. No archive, graph or provider semantics. */
#include <jni.h>
#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cgw_webview_bridge.h"

#define ACTIVITY "org/isomorphisms/webview/BridgeActivity"
#define CLIENT "org/isomorphisms/webview/BridgeClient"
#define CALLBACK "org/isomorphisms/webview/BridgeMessageCallback"
#define TAG "CGW-E1"
static jobject activity, webview, active_port, status_view;
static cgw_webview_bridge *bridge;
static char bootstrap[33];
static int port_verified;
#ifdef CGW_WEBVIEW_SYNTHETIC
#include <stdatomic.h>
static atomic_int fixture;
#endif

static jmethodID method(JNIEnv *env, jobject object, const char *name,
                         const char *signature)
{
    if (object == NULL || (*env)->ExceptionCheck(env)) return NULL;
    jclass cls = (*env)->GetObjectClass(env, object);
    if (!cls) return NULL;
    jmethodID result = (*env)->GetMethodID(env, cls, name, signature);
    (*env)->DeleteLocalRef(env, cls);
    return result;
}

static jobject construct(JNIEnv *env, const char *name)
{
    jclass cls = (*env)->FindClass(env, name);
    if (!cls) return NULL;
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "()V");
    jobject object = init ? (*env)->NewObject(env, cls, init) : NULL;
    (*env)->DeleteLocalRef(env, cls);
    return object;
}

static int entropy(void *context, unsigned char *bytes, size_t length)
{
    (void)context;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = read(fd, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { close(fd); return 0; }
        offset += (size_t)count;
    }
    close(fd);
    return 1;
}

static void report(const char *result)
{
    /* Never log URLs, page bodies, credentials, nonces or message contents. */
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", result);
}

static void show_status(JNIEnv *env, const char *text)
{
    if (!activity) return;
    jmethodID title = method(env, status_view, "setText", "(Ljava/lang/CharSequence;)V");
    jstring value = (*env)->NewStringUTF(env, text);
    if (title && value) (*env)->CallVoidMethod(env, status_view, title, value);
    if (value) (*env)->DeleteLocalRef(env, value);
}

static int trusted_url(JNIEnv *env, jstring value)
{
    if (!value) return 0;
    const char *text = (*env)->GetStringUTFChars(env, value, NULL);
    if (!text) return 0;
    static const char origin[] = "https://chatgpt.com";
    size_t prefix = sizeof(origin) - 1;
    size_t length = strlen(text);
    int valid = length >= prefix && memcmp(text, origin, prefix) == 0 &&
                (length == prefix || text[prefix] == '/' || text[prefix] == '?' ||
                 text[prefix] == '#');
    (*env)->ReleaseStringUTFChars(env, value, text);
    return valid;
}

static int login_url(JNIEnv *env, jstring value)
{
    if (!value) return 0;
    const char *text = (*env)->GetStringUTFChars(env, value, NULL);
    if (!text) return 0;
    static const char origin[] = "https://auth.openai.com";
    size_t prefix = sizeof(origin) - 1;
    size_t length = strlen(text);
    int valid = length >= prefix && memcmp(text, origin, prefix) == 0 &&
                (length == prefix || text[prefix] == '/' || text[prefix] == '?' || text[prefix] == '#');
    (*env)->ReleaseStringUTFChars(env, value, text);
    return valid;
}

static int current_document(JNIEnv *env, jobject view)
{
    if (!webview || !(*env)->IsSameObject(env, webview, view)) return 0;
    jmethodID get = method(env, view, "getUrl", "()Ljava/lang/String;");
    if (!get) return 0;
    jstring url = (*env)->CallObjectMethod(env, view, get);
    int valid = trusted_url(env, url);
    if (url) (*env)->DeleteLocalRef(env, url);
    return valid;
}

static void revoke(JNIEnv *env)
{
    port_verified = 0;
    if (active_port) {
        jmethodID close = method(env, active_port, "close", "()V");
        if (close) (*env)->CallVoidMethod(env, active_port, close);
        (*env)->DeleteGlobalRef(env, active_port);
        active_port = NULL;
    }
    if (bridge && cgw_webview_bridge_navigation_started(bridge) == CGW_WEBVIEW_NAVIGATION_CANCELLED)
        report("navigation:request_cancelled");
    memset(bootstrap, 0, sizeof(bootstrap));
}

static int discard(void *context, const unsigned char *bytes, size_t length)
{
    (void)context; (void)bytes; (void)length;
    /* E1 is a consented transport probe, not an export or raw archive sink. */
    return 1;
}

static void post(JNIEnv *env, jobject port, const char *wire)
{
    jclass cls = (*env)->FindClass(env, "android/webkit/WebMessage");
    if (!cls) return;
    jmethodID init = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
    jstring text = (*env)->NewStringUTF(env, wire);
    jobject message = init && text ? (*env)->NewObject(env, cls, init, text) : NULL;
    jmethodID send = method(env, port, "postMessage", "(Landroid/webkit/WebMessage;)V");
    if (message && send) (*env)->CallVoidMethod(env, port, send, message);
    if (message) (*env)->DeleteLocalRef(env, message);
    if (text) (*env)->DeleteLocalRef(env, text);
    (*env)->DeleteLocalRef(env, cls);
}

static void probe(JNIEnv *env)
{
    if (!bridge || !port_verified || !current_document(env, webview)) {
        report("probe:not_ready"); show_status(env, "Session check not ready - wait for the page to load"); return;
    }
    cgw_input input = { .operation = CGW_ACCOUNTS_LIST };
    cgw_webview_command command;
    cgw_webview_result result = cgw_webview_bridge_begin(
        bridge, &input, discard, NULL, &command);
    if (result != CGW_WEBVIEW_OK) { report(cgw_webview_result_name(result)); show_status(env, "Session check unavailable - wait or reload the page"); return; }
    show_status(env, "Checking session...");
    post(env, active_port, command.wire);
}

static char *asset(JNIEnv *env, const char *name)
{
    jmethodID get = method(env, activity, "getAssets", "()Landroid/content/res/AssetManager;");
    if (!get) return NULL;
    jobject assets = (*env)->CallObjectMethod(env, activity, get);
    jmethodID open = method(env, assets, "open", "(Ljava/lang/String;)Ljava/io/InputStream;");
    jstring path = (*env)->NewStringUTF(env, name);
    jobject stream = open && path ? (*env)->CallObjectMethod(env, assets, open, path) : NULL;
    jmethodID read_method = method(env, stream, "read", "([B)I");
    char *result = calloc(65537, 1);
    jbyteArray chunk = (*env)->NewByteArray(env, 4096);
    size_t length = 0;
    int valid = result && chunk && read_method;
    while (valid) {
        jint count = (*env)->CallIntMethod(env, stream, read_method, chunk);
        if ((*env)->ExceptionCheck(env)) { valid = 0; break; }
        if (count == -1) break;
        if (count <= 0 || count > 4096 || (size_t)count > 65536 - length) {
            valid = 0; break;
        }
        (*env)->GetByteArrayRegion(env, chunk, 0, count, (jbyte *)result + length);
        length += (size_t)count;
    }
    jmethodID close = method(env, stream, "close", "()V");
    if (close) (*env)->CallVoidMethod(env, stream, close);
    if (chunk) (*env)->DeleteLocalRef(env, chunk);
    if (stream) (*env)->DeleteLocalRef(env, stream);
    if (assets) (*env)->DeleteLocalRef(env, assets);
    if (path) (*env)->DeleteLocalRef(env, path);
    if (!valid) { free(result); return NULL; }
    return result;
}

static void bootstrap_port(JNIEnv *env, jobject view, jstring url)
{
    if (active_port || !trusted_url(env, url) || !current_document(env, view)) return;
    unsigned char random[16];
    if (!entropy(NULL, random, sizeof(random))) return;
    for (size_t i = 0; i < sizeof(random); ++i)
        snprintf(bootstrap + i * 2, 3, "%02x", random[i]);
    char *source = asset(env, "bridge.js");
    if (!source) { report("bootstrap:asset_failure"); return; }
    char *marker = strstr(source, "__CGW_BOOTSTRAP__");
    if (!marker) { free(source); report("bootstrap:marker_missing"); return; }
    size_t old_length = strlen(source);
    size_t prefix = (size_t)(marker - source);
    char *expanded = malloc(old_length + 33);
    if (!expanded) { free(source); return; }
    memcpy(expanded, source, prefix);
    memcpy(expanded + prefix, bootstrap, 32);
    strcpy(expanded + prefix + 32, marker + strlen("__CGW_BOOTSTRAP__"));
    free(source);
    jmethodID evaluate = method(env, view, "evaluateJavascript",
                                "(Ljava/lang/String;Landroid/webkit/ValueCallback;)V");
    jstring script = (*env)->NewStringUTF(env, expanded);
    free(expanded);
    if (!evaluate || !script) return;
    (*env)->CallVoidMethod(env, view, evaluate, script, NULL);
    (*env)->DeleteLocalRef(env, script);
    jmethodID create = method(env, view, "createWebMessageChannel", "()[Landroid/webkit/WebMessagePort;");
    if (!create) return;
    jobjectArray ports = (*env)->CallObjectMethod(env, view, create);
    if (!ports || (*env)->GetArrayLength(env, ports) != 2) return;
    jobject local = (*env)->GetObjectArrayElement(env, ports, 0);
    jobject remote = (*env)->GetObjectArrayElement(env, ports, 1);
    jobject callback = construct(env, CALLBACK);
    jmethodID set = method(env, local, "setWebMessageCallback",
                           "(Landroid/webkit/WebMessagePort$WebMessageCallback;)V");
    if (!set || !callback) return;
    (*env)->CallVoidMethod(env, local, set, callback);
    active_port = (*env)->NewGlobalRef(env, local);
    jclass port_class = (*env)->FindClass(env, "android/webkit/WebMessagePort");
    jobjectArray transferred = (*env)->NewObjectArray(env, 1, port_class, remote);
    jclass message_class = (*env)->FindClass(env, "android/webkit/WebMessage");
    jmethodID init = message_class ? (*env)->GetMethodID(env, message_class, "<init>",
        "(Ljava/lang/String;[Landroid/webkit/WebMessagePort;)V") : NULL;
    jstring nonce = (*env)->NewStringUTF(env, bootstrap);
    jobject message = init ? (*env)->NewObject(env, message_class, init, nonce, transferred) : NULL;
    jclass uri_class = (*env)->FindClass(env, "android/net/Uri");
    jmethodID parse = uri_class ? (*env)->GetStaticMethodID(env, uri_class, "parse",
        "(Ljava/lang/String;)Landroid/net/Uri;") : NULL;
    jstring origin = (*env)->NewStringUTF(env, "https://chatgpt.com");
    jobject uri = parse ? (*env)->CallStaticObjectMethod(env, uri_class, parse, origin) : NULL;
    jmethodID send = method(env, view, "postWebMessage",
                            "(Landroid/webkit/WebMessage;Landroid/net/Uri;)V");
    if (!message || !uri || !send || (*env)->ExceptionCheck(env)) { revoke(env); return; }
    if (cgw_webview_bridge_document_committed(bridge, "https://chatgpt.com", 1) != CGW_WEBVIEW_OK) {
        revoke(env); return;
    }
    (*env)->CallVoidMethod(env, view, send, message, uri);
    report("bootstrap:port_transferred_to_exact_origin");
    /* All locals created in a native callback are released on callback return. */
}

static void on_message(JNIEnv *env, jobject self, jobject port, jobject message)
{
    (void)self;
    if (!active_port || !(*env)->IsSameObject(env, active_port, port) ||
        !current_document(env, webview)) { report("message:stale_port_rejected"); return; }
    jmethodID get = method(env, message, "getData", "()Ljava/lang/String;");
    if (!get) return;
    jstring value = (*env)->CallObjectMethod(env, message, get);
    if (!value || (*env)->GetStringLength(env, value) > CGW_WEBVIEW_MESSAGE_CAPACITY) {
        report("message:oversize_rejected"); return;
    }
    const char *wire = (*env)->GetStringUTFChars(env, value, NULL);
    if (!wire) return;
    char expected[64];
    snprintf(expected, sizeof(expected), "CGW1;ready;%s", bootstrap);
    if (!port_verified && strcmp(wire, expected) == 0) {
        if (cgw_webview_bridge_port_ready(bridge) == CGW_WEBVIEW_OK) {
            port_verified = 1;
            report("port:ready");
            show_status(env, "Ready - tap Check session");
#ifdef CGW_WEBVIEW_SYNTHETIC
            if (atomic_load(&fixture) != 0) probe(env);
#endif
        }
    } else if (port_verified) {
        cgw_webview_result result = cgw_webview_bridge_receive(bridge, wire, strlen(wire));
        report(cgw_webview_result_name(result));
        if (result != CGW_WEBVIEW_OK && result != CGW_WEBVIEW_HTTP_FAILURE)
            show_status(env, "Session check rejected - tap Check session to retry");
        cgw_webview_observation observation;
        cgw_webview_bridge_observation(bridge, &observation);
        if (result == CGW_WEBVIEW_HTTP_FAILURE) {
            char status[32];
            snprintf(status, sizeof(status), "probe:http:%u", observation.http_status);
            report(status);
            show_status(env, observation.http_status == 401 || observation.http_status == 403
                                ? "Sign in on the page, then check session"
                                : "Session check failed - tap Check session to retry");
        }
        if (observation.complete) {
            report("probe:complete");
            show_status(env, "Session check passed - no archive written");
        }
    } else report("message:bootstrap_rejected");
    (*env)->ReleaseStringUTFChars(env, value, wire);
}

static void on_started(JNIEnv *env, jobject self, jobject view, jstring url, jobject icon)
{
    (void)self; (void)icon;
    if (webview && (*env)->IsSameObject(env, webview, view)) {
        revoke(env); report("navigation:revoked");
        show_status(env, "Page loading - session check not ready");
        if (!trusted_url(env, url) && !login_url(env, url)) {
            jmethodID stop = method(env, view, "stopLoading", "()V");
            if (stop) (*env)->CallVoidMethod(env, view, stop);
            report("navigation:external_or_federated_refused");
            show_status(env, "External or federated sign-in refused");
        }
    }
}

static void on_committed(JNIEnv *env, jobject self, jobject view, jstring url)
{
    (void)self;
    bootstrap_port(env, view, url);
}

static jboolean override_url(JNIEnv *env, jobject self, jobject view, jobject request)
{
    (void)self; (void)view;
    jmethodID get = method(env, request, "getUrl", "()Landroid/net/Uri;");
    jobject uri = get ? (*env)->CallObjectMethod(env, request, get) : NULL;
    jmethodID to_string = method(env, uri, "toString", "()Ljava/lang/String;");
    jstring url = to_string ? (*env)->CallObjectMethod(env, uri, to_string) : NULL;
    /* E1 supports first-party navigation. Federated providers are explicitly
     * refused until their sign-in path has its own qualification. */
    if (!trusted_url(env, url) && !login_url(env, url)) {
        report("navigation:external_or_federated_refused");
        show_status(env, "External or federated sign-in refused");
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

static void ssl_error(JNIEnv *env, jobject self, jobject view, jobject handler, jobject error)
{
    (void)self; (void)view; (void)error;
    revoke(env);
    jmethodID cancel = method(env, handler, "cancel", "()V");
    if (cancel) (*env)->CallVoidMethod(env, handler, cancel);
    report("tls:rejected");
}

static jobject intercept(JNIEnv *env, jobject self, jobject view, jobject request)
{
#ifdef CGW_WEBVIEW_SYNTHETIC
    (void)self; (void)view;
    /* Test-only transport: intercept EVERY request, so the emulator fixture
     * cannot contact ChatGPT or a login provider. This branch is absent from
     * the production binary and uses no real account/session. */
    jmethodID get = method(env, request, "getUrl", "()Landroid/net/Uri;");
    jobject uri = get ? (*env)->CallObjectMethod(env, request, get) : NULL;
    jmethodID string = method(env, uri, "toString", "()Ljava/lang/String;");
    jstring value = string ? (*env)->CallObjectMethod(env, uri, string) : NULL;
    const char *url = value ? (*env)->GetStringUTFChars(env, value, NULL) : NULL;
    const char *body = "{}";
    const char *mime = "application/json";
    int status = 200;
    int mode = atomic_load(&fixture);
    if (url && strcmp(url, "https://chatgpt.com/e1-fixture") == 0) {
        mime = "text/html";
        body = "<!doctype html><meta charset=utf-8><title>E1 synthetic sign-in</title>"
               "<h1>Synthetic fixture only</h1><p>No live account or network</p>";
        if (mode == 5) body =
            "<!doctype html><title>E1 interruption fixture</title><script>"
            "const originalFetch=fetch;window.fetch=(path,opts)=>path.startsWith('/backend-api/')?"
            "new Promise(resolve=>setTimeout(()=>resolve(originalFetch(path,opts)),10000)):originalFetch(path,opts);"
            "addEventListener('message',e=>{if(e.ports.length===1)setTimeout(()=>"
            "location.href='/e1-interrupted',1000)});</script>";
        if (mode == 6) body =
            "<!doctype html><title>E1 origin fixture</title>"
            "<script>addEventListener('message',e=>{if(e.ports.length===1)setTimeout(()=>"
            "location.href='https://evil.example/',1000)});</script>";
        if (mode == 7) body =
            "<!doctype html><title>E1 oversize message fixture</title><script>"
            "addEventListener('message',e=>{if(e.ports.length===1)setTimeout(()=>"
            "e.ports[0].postMessage('x'.repeat(16385)),300)});</script>";
        if (mode == 8) body =
            "<!doctype html><title>E1 forged message fixture</title><script>"
            "const originalFetch=fetch;window.fetch=(path,opts)=>path.startsWith('/backend-api/')?"
            "new Promise(resolve=>setTimeout(()=>resolve(originalFetch(path,opts)),2000)):originalFetch(path,opts);"
            "addEventListener('message',e=>{if(e.ports.length===1)setTimeout(()=>"
            "e.ports[0].postMessage('CGW1;reply;00000000000000000000000000000000;999;200;0;1;eA'),300)});</script>";
    } else if (url && strcmp(url, "https://chatgpt.com/api/auth/session") == 0) {
        body = mode == 1 ? "{}" : "{\"accessToken\":\"e1-synthetic-token\"}";
    } else if (url && strstr(url, "https://chatgpt.com/backend-api/") == url) {
        body = "{\"accounts\":[],\"accessToken\":\"must-not-cross\"}";
        if (mode == 2) status = 401;
        if (mode == 4) { mime = "text/html"; body = "schema drift"; }
    } else { status = 403; }
    if (url) (*env)->ReleaseStringUTFChars(env, value, url);
    size_t length = strlen(body);
    int oversized = mode == 3 && strcmp(mime, "application/json") == 0 &&
                    strcmp(body, "{\"accounts\":[],\"accessToken\":\"must-not-cross\"}") == 0;
    if (oversized) length = 5000001;
    jbyteArray bytes = (*env)->NewByteArray(env, (jsize)length);
    if (!bytes) return NULL;
    if (!oversized) (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)length, (const jbyte *)body);
    jclass input_class = (*env)->FindClass(env, "java/io/ByteArrayInputStream");
    jmethodID input_init = input_class ? (*env)->GetMethodID(env, input_class, "<init>", "([B)V") : NULL;
    jobject input = input_init ? (*env)->NewObject(env, input_class, input_init, bytes) : NULL;
    jclass response_class = (*env)->FindClass(env, "android/webkit/WebResourceResponse");
    jmethodID response_init = response_class ? (*env)->GetMethodID(env, response_class, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/util/Map;Ljava/io/InputStream;)V") : NULL;
    jstring type = (*env)->NewStringUTF(env, mime);
    jstring encoding = (*env)->NewStringUTF(env, "UTF-8");
    jstring reason = (*env)->NewStringUTF(env, status == 200 ? "OK" : "Synthetic refusal");
    if (!response_init || !input || !type || !encoding || !reason) return NULL;
    return (*env)->NewObject(env, response_class, response_init, type, encoding, status, reason, NULL, input);
#else
    (void)env; (void)self; (void)view; (void)request;
    return NULL; /* No network substitution in the production build. */
#endif
}

static jboolean create_menu(JNIEnv *env, jobject self, jobject menu)
{
    (void)self;
    jmethodID add = method(env, menu, "add", "(IIILjava/lang/CharSequence;)Landroid/view/MenuItem;");
    jstring title = (*env)->NewStringUTF(env, "Check session");
    if (!add || !title) return JNI_FALSE;
    jobject item = (*env)->CallObjectMethod(env, menu, add, 0, 1, 0, title);
    jmethodID show = method(env, item, "setShowAsAction", "(I)V");
    if (show) (*env)->CallVoidMethod(env, item, show, 6); /* ALWAYS | WITH_TEXT */
    return JNI_TRUE;
}

static jboolean select_menu(JNIEnv *env, jobject self, jobject item)
{
    (void)self;
    jmethodID get = method(env, item, "getItemId", "()I");
    if (get && (*env)->CallIntMethod(env, item, get) == 1) { probe(env); return JNI_TRUE; }
    return JNI_FALSE;
}

static void on_back(JNIEnv *env, jobject self)
{
    jmethodID can = method(env, webview, "canGoBack", "()Z");
    if (can && (*env)->CallBooleanMethod(env, webview, can)) {
        revoke(env);
        jmethodID go = method(env, webview, "goBack", "()V");
        if (go) (*env)->CallVoidMethod(env, webview, go);
    } else {
        jmethodID finish = method(env, self, "finish", "()V");
        if (finish) (*env)->CallVoidMethod(env, self, finish);
    }
}


/* Native controls remain outside the web page and its sidebar. The toolbar
 * routes its always-visible action through the existing Activity menu callback. */
static jobject context_view(JNIEnv *env, const char *name)
{
    jclass cls = (*env)->FindClass(env, name);
    jmethodID init = cls ? (*env)->GetMethodID(env, cls, "<init>", "(Landroid/content/Context;)V") : NULL;
    return init ? (*env)->NewObject(env, cls, init, activity) : NULL;
}

static int install_controls(JNIEnv *env, jobject view)
{
    jobject column = context_view(env, "android/widget/LinearLayout");
    jobject toolbar = context_view(env, "android/widget/Toolbar");
    jobject status = context_view(env, "android/widget/TextView");
    if (!column || !toolbar || !status) return 0;
    jmethodID orientation = method(env, column, "setOrientation", "(I)V");
    jmethodID add = method(env, column, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    jmethodID actionbar = method(env, activity, "setActionBar", "(Landroid/widget/Toolbar;)V");
    jmethodID content = method(env, activity, "setContentView", "(Landroid/view/View;)V");
    jclass params_class = (*env)->FindClass(env, "android/widget/LinearLayout$LayoutParams");
    jmethodID params = params_class ? (*env)->GetMethodID(env, params_class, "<init>", "(IIF)V") : NULL;
    if (!orientation || !add || !actionbar || !content || !params) return 0;
    (*env)->CallVoidMethod(env, column, orientation, 1);
    jobject wrap = (*env)->NewObject(env, params_class, params, -1, -2, 0.0f);
    jobject status_wrap = (*env)->NewObject(env, params_class, params, -1, -2, 0.0f);
    jobject fill = (*env)->NewObject(env, params_class, params, -1, 0, 1.0f);
    if (!wrap || !status_wrap || !fill) return 0;
    (*env)->CallVoidMethod(env, column, add, toolbar, wrap);
    (*env)->CallVoidMethod(env, column, add, status, status_wrap);
    (*env)->CallVoidMethod(env, column, add, view, fill);
    (*env)->CallVoidMethod(env, activity, content, column);
    (*env)->CallVoidMethod(env, activity, actionbar, toolbar);
    jmethodID get_bar = method(env, activity, "getActionBar", "()Landroid/app/ActionBar;");
    jobject bar = get_bar ? (*env)->CallObjectMethod(env, activity, get_bar) : NULL;
    jmethodID title_visible = method(env, bar, "setDisplayShowTitleEnabled", "(Z)V");
    if (title_visible) (*env)->CallVoidMethod(env, bar, title_visible, JNI_FALSE);
    status_view = (*env)->NewGlobalRef(env, status);
    show_status(env, "Page loading - session check not ready");
    return !(*env)->ExceptionCheck(env);
}

static void on_create(JNIEnv *env, jobject self, jobject bundle)
{
    jclass base = (*env)->FindClass(env, "android/app/Activity");
    jmethodID super = base ? (*env)->GetMethodID(env, base, "onCreate", "(Landroid/os/Bundle;)V") : NULL;
    if (!super) return;
    (*env)->CallNonvirtualVoidMethod(env, self, base, super, bundle);
    if ((*env)->ExceptionCheck(env)) return;
    if (activity) { report("lifecycle:duplicate_activity_refused"); return; }
    activity = (*env)->NewGlobalRef(env, self);
    cgw_webview_bridge_config config = { .entropy = entropy, .response_cap_bytes = 5000000 };
    bridge = cgw_webview_bridge_open(&config);
    if (!bridge) return;
    jclass cls = (*env)->FindClass(env, "android/webkit/WebView");
    jmethodID init = cls ? (*env)->GetMethodID(env, cls, "<init>", "(Landroid/content/Context;)V") : NULL;
    jobject view = init ? (*env)->NewObject(env, cls, init, self) : NULL;
    if (!view) return;
    webview = (*env)->NewGlobalRef(env, view);
    jmethodID get_settings = method(env, view, "getSettings", "()Landroid/webkit/WebSettings;");
    jobject settings = get_settings ? (*env)->CallObjectMethod(env, view, get_settings) : NULL;
    const char *booleans[] = {"setJavaScriptEnabled", "setDomStorageEnabled", "setAllowFileAccess",
                              "setAllowContentAccess", "setJavaScriptCanOpenWindowsAutomatically",
                              "setSupportMultipleWindows", "setSafeBrowsingEnabled"};
    const jboolean values[] = {JNI_TRUE, JNI_TRUE, JNI_FALSE, JNI_FALSE, JNI_FALSE, JNI_FALSE, JNI_TRUE};
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        jmethodID set = method(env, settings, booleans[i], "(Z)V");
        if (!set) return;
        (*env)->CallVoidMethod(env, settings, set, values[i]);
    }
    jmethodID mixed = method(env, settings, "setMixedContentMode", "(I)V");
    if (!mixed) return;
    (*env)->CallVoidMethod(env, settings, mixed, 1); /* MIXED_CONTENT_NEVER_ALLOW */
    jobject client = construct(env, CLIENT);
    jmethodID set_client = method(env, view, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V");
    jmethodID content = method(env, self, "setContentView", "(Landroid/view/View;)V");
    jmethodID load = method(env, view, "loadUrl", "(Ljava/lang/String;)V");
    if (!client || !set_client || !content || !load) return;
    (*env)->CallVoidMethod(env, view, set_client, client);
    if (!install_controls(env, view)) return;
#ifdef CGW_WEBVIEW_SYNTHETIC
    jmethodID get_intent = method(env, self, "getIntent", "()Landroid/content/Intent;");
    jobject intent = get_intent ? (*env)->CallObjectMethod(env, self, get_intent) : NULL;
    jmethodID extra = method(env, intent, "getIntExtra", "(Ljava/lang/String;I)I");
    jstring key = (*env)->NewStringUTF(env, "fixture");
    atomic_store(&fixture, extra ? (*env)->CallIntMethod(env, intent, extra, key, 0) : 0);
    jstring url = (*env)->NewStringUTF(env, "https://chatgpt.com/e1-fixture");
#else
    jstring url = (*env)->NewStringUTF(env, "https://chatgpt.com/");
#endif
    (*env)->CallVoidMethod(env, view, load, url);
    if (!(*env)->ExceptionCheck(env)) report("shell:interactive_sign_in_ready");
}

static void on_destroy(JNIEnv *env, jobject self)
{
    if (activity && (*env)->IsSameObject(env, activity, self)) {
        revoke(env);
        if (webview) {
            jclass layout = (*env)->FindClass(env, "android/widget/FrameLayout");
            jmethodID init = layout ? (*env)->GetMethodID(env, layout, "<init>", "(Landroid/content/Context;)V") : NULL;
            jobject empty = init ? (*env)->NewObject(env, layout, init, self) : NULL;
            jmethodID content = method(env, self, "setContentView", "(Landroid/view/View;)V");
            if (empty && content) (*env)->CallVoidMethod(env, self, content, empty);
            jmethodID destroy = method(env, webview, "destroy", "()V");
            if (destroy) (*env)->CallVoidMethod(env, webview, destroy);
            (*env)->DeleteGlobalRef(env, webview); webview = NULL;
        }
        if (status_view) { (*env)->DeleteGlobalRef(env, status_view); status_view = NULL; }
        cgw_webview_bridge_close(bridge); bridge = NULL;
        (*env)->DeleteGlobalRef(env, activity); activity = NULL;
    }
    jclass base = (*env)->FindClass(env, "android/app/Activity");
    jmethodID super = base ? (*env)->GetMethodID(env, base, "onDestroy", "()V") : NULL;
    if (super) (*env)->CallNonvirtualVoidMethod(env, self, base, super);
}

/* Conventional JNI symbol resolution avoids initializing the other DEX
 * classes recursively while Activity.<clinit> is loading this library. */
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeActivity_onCreate
(JNIEnv *e, jobject s, jobject b) { on_create(e,s,b); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeActivity_onDestroy
(JNIEnv *e, jobject s) { on_destroy(e,s); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeActivity_onBackPressed
(JNIEnv *e, jobject s) { on_back(e,s); }
JNIEXPORT jboolean JNICALL Java_org_isomorphisms_webview_BridgeActivity_onCreateOptionsMenu
(JNIEnv *e, jobject s, jobject m) { return create_menu(e,s,m); }
JNIEXPORT jboolean JNICALL Java_org_isomorphisms_webview_BridgeActivity_onOptionsItemSelected
(JNIEnv *e, jobject s, jobject m) { return select_menu(e,s,m); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeClient_onPageStarted
(JNIEnv *e, jobject s, jobject v, jstring u, jobject i) { on_started(e,s,v,u,i); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeClient_onPageCommitVisible
(JNIEnv *e, jobject s, jobject v, jstring u) { on_committed(e,s,v,u); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeClient_onPageFinished
(JNIEnv *e, jobject s, jobject v, jstring u) { on_committed(e,s,v,u); }
JNIEXPORT jboolean JNICALL Java_org_isomorphisms_webview_BridgeClient_shouldOverrideUrlLoading
(JNIEnv *e, jobject s, jobject v, jobject r) { return override_url(e,s,v,r); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeClient_onReceivedSslError
(JNIEnv *e, jobject s, jobject v, jobject h, jobject x) { ssl_error(e,s,v,h,x); }
JNIEXPORT jobject JNICALL Java_org_isomorphisms_webview_BridgeClient_shouldInterceptRequest
(JNIEnv *e, jobject s, jobject v, jobject r) { return intercept(e,s,v,r); }
JNIEXPORT void JNICALL Java_org_isomorphisms_webview_BridgeMessageCallback_onMessage
(JNIEnv *e, jobject s, jobject p, jobject m) { on_message(e,s,p,m); }
