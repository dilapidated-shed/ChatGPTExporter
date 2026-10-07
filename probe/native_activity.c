#include <android/log.h>
#include <android/native_activity.h>
#include <jni.h>
#include <stddef.h>

#define TAG "ChatGPTWebProbe"

static int clear_exception(JNIEnv *env, const char *where) {
    if (!(*env)->ExceptionCheck(env)) return 0;
    __android_log_print(ANDROID_LOG_ERROR, TAG, "Java exception at %s", where);
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
    return 1;
}

static jclass find_class(JNIEnv *env, const char *name) {
    jclass cls = (*env)->FindClass(env, name);
    if (cls == NULL || clear_exception(env, name)) return NULL;
    return cls;
}

static jmethodID method(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    jmethodID id = (*env)->GetMethodID(env, cls, name, sig);
    if (id == NULL || clear_exception(env, name)) return NULL;
    return id;
}

static jmethodID static_method(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    jmethodID id = (*env)->GetStaticMethodID(env, cls, name, sig);
    if (id == NULL || clear_exception(env, name)) return NULL;
    return id;
}

static int call_bool(JNIEnv *env, jobject obj, jclass cls, const char *name, jboolean value) {
    jmethodID id = method(env, cls, name, "(Z)V");
    if (id == NULL) return 0;
    (*env)->CallVoidMethod(env, obj, id, value);
    return !clear_exception(env, name);
}

void ANativeActivity_onCreate(ANativeActivity *activity, void *saved_state, size_t saved_state_size) {
    (void)saved_state;
    (void)saved_state_size;

    JNIEnv *env = activity->env;
    jobject host = activity->clazz;

    jclass webview_cls = find_class(env, "android/webkit/WebView");
    jclass settings_cls = find_class(env, "android/webkit/WebSettings");
    jclass cookie_cls = find_class(env, "android/webkit/CookieManager");
    jclass client_cls = find_class(env, "android/webkit/WebViewClient");
    jclass chrome_client_cls = find_class(env, "android/webkit/WebChromeClient");
    jclass activity_cls = find_class(env, "android/app/Activity");
    jclass window_cls = find_class(env, "android/view/Window");
    if (!webview_cls || !settings_cls || !cookie_cls || !client_cls ||
        !chrome_client_cls || !activity_cls || !window_cls) return;

    /*
     * NativeActivity claims the Window surface and InputQueue before loading
     * this library. This probe wants an ordinary Android view hierarchy
     * instead, so return both to PhoneWindow before installing the WebView.
     */
    jmethodID get_window = method(
        env, activity_cls, "getWindow", "()Landroid/view/Window;");
    if (!get_window) return;
    jobject window = (*env)->CallObjectMethod(env, host, get_window);
    if (!window || clear_exception(env, "getWindow")) return;

    jmethodID take_surface = method(
        env, window_cls, "takeSurface", "(Landroid/view/SurfaceHolder$Callback2;)V");
    jmethodID take_input_queue = method(
        env, window_cls, "takeInputQueue", "(Landroid/view/InputQueue$Callback;)V");
    if (!take_surface || !take_input_queue) return;
    (*env)->CallVoidMethod(env, window, take_surface, NULL);
    if (clear_exception(env, "takeSurface(null)")) return;
    (*env)->CallVoidMethod(env, window, take_input_queue, NULL);
    if (clear_exception(env, "takeInputQueue(null)")) return;

    jmethodID enable_debug = static_method(
        env, webview_cls, "setWebContentsDebuggingEnabled", "(Z)V");
    if (!enable_debug) return;
    (*env)->CallStaticVoidMethod(env, webview_cls, enable_debug, JNI_TRUE);
    if (clear_exception(env, "setWebContentsDebuggingEnabled")) return;

    jmethodID webview_ctor = method(
        env, webview_cls, "<init>", "(Landroid/content/Context;)V");
    if (!webview_ctor) return;
    jobject webview = (*env)->NewObject(env, webview_cls, webview_ctor, host);
    if (!webview || clear_exception(env, "WebView constructor")) return;

    jmethodID get_settings = method(
        env, webview_cls, "getSettings", "()Landroid/webkit/WebSettings;");
    if (!get_settings) return;
    jobject settings = (*env)->CallObjectMethod(env, webview, get_settings);
    if (!settings || clear_exception(env, "getSettings")) return;

    if (!call_bool(env, settings, settings_cls, "setJavaScriptEnabled", JNI_TRUE)) return;
    if (!call_bool(env, settings, settings_cls, "setDomStorageEnabled", JNI_TRUE)) return;
    if (!call_bool(env, settings, settings_cls, "setDatabaseEnabled", JNI_TRUE)) return;
    if (!call_bool(env, settings, settings_cls, "setAllowFileAccess", JNI_FALSE)) return;
    if (!call_bool(env, settings, settings_cls, "setAllowContentAccess", JNI_FALSE)) return;
    if (!call_bool(env, settings, settings_cls, "setSupportMultipleWindows", JNI_FALSE)) return;
    if (!call_bool(env, settings, settings_cls, "setJavaScriptCanOpenWindowsAutomatically", JNI_TRUE)) return;

    jmethodID mixed_content = method(env, settings_cls, "setMixedContentMode", "(I)V");
    if (!mixed_content) return;
    (*env)->CallVoidMethod(env, settings, mixed_content, 1);
    if (clear_exception(env, "setMixedContentMode")) return;

    jmethodID get_cookie_manager = static_method(
        env, cookie_cls, "getInstance", "()Landroid/webkit/CookieManager;");
    if (!get_cookie_manager) return;
    jobject cookies = (*env)->CallStaticObjectMethod(env, cookie_cls, get_cookie_manager);
    if (!cookies || clear_exception(env, "CookieManager.getInstance")) return;

    if (!call_bool(env, cookies, cookie_cls, "setAcceptCookie", JNI_TRUE)) return;
    jmethodID third_party = method(
        env, cookie_cls, "setAcceptThirdPartyCookies", "(Landroid/webkit/WebView;Z)V");
    if (!third_party) return;
    (*env)->CallVoidMethod(env, cookies, third_party, webview, JNI_TRUE);
    if (clear_exception(env, "setAcceptThirdPartyCookies")) return;

    jmethodID client_ctor = method(env, client_cls, "<init>", "()V");
    jmethodID set_client = method(
        env, webview_cls, "setWebViewClient", "(Landroid/webkit/WebViewClient;)V");
    if (!client_ctor || !set_client) return;
    jobject client = (*env)->NewObject(env, client_cls, client_ctor);
    if (!client || clear_exception(env, "WebViewClient constructor")) return;
    (*env)->CallVoidMethod(env, webview, set_client, client);
    if (clear_exception(env, "setWebViewClient")) return;

    jmethodID chrome_ctor = method(env, chrome_client_cls, "<init>", "()V");
    jmethodID set_chrome_client = method(
        env, webview_cls, "setWebChromeClient", "(Landroid/webkit/WebChromeClient;)V");
    if (!chrome_ctor || !set_chrome_client) return;
    jobject chrome_client = (*env)->NewObject(env, chrome_client_cls, chrome_ctor);
    if (!chrome_client || clear_exception(env, "WebChromeClient constructor")) return;
    (*env)->CallVoidMethod(env, webview, set_chrome_client, chrome_client);
    if (clear_exception(env, "setWebChromeClient")) return;

    jmethodID set_content_view = method(
        env, activity_cls, "setContentView", "(Landroid/view/View;)V");
    if (!set_content_view) return;
    (*env)->CallVoidMethod(env, host, set_content_view, webview);
    if (clear_exception(env, "setContentView")) return;

    jmethodID load_url = method(
        env, webview_cls, "loadUrl", "(Ljava/lang/String;)V");
    if (!load_url) return;
    jstring home = (*env)->NewStringUTF(env, "https://chatgpt.com/");
    if (!home || clear_exception(env, "NewStringUTF")) return;
    (*env)->CallVoidMethod(env, webview, load_url, home);
    if (clear_exception(env, "loadUrl")) return;

    __android_log_print(ANDROID_LOG_INFO, TAG, "WebView started; debugging enabled");
}
