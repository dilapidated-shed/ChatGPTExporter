/* Included by the Android adapter: all bridge state is protected by
 * platform_mutex. The worker never calls a WebView or WebMessagePort method. */
#include <dirent.h>

static int safe_workspace(const char *id)
{
    size_t length = strlen(id);
    if (!length || length > 256) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!((id[i] >= 'A' && id[i] <= 'Z') || (id[i] >= 'a' && id[i] <= 'z')
           || (id[i] >= '0' && id[i] <= '9') || id[i] == '_' || id[i] == '-')) return 0;
    return 1;
}
static int account_sink(void *unused, const unsigned char *bytes, size_t length)
{
    (void)unused;
    if (length > sizeof(account_bytes) - account_used) return 0;
    memcpy(account_bytes + account_used, bytes, length); account_used += length; return 1;
}
static int accept_accounts(JNIEnv *env)
{
    cj_document d;
    if (!cj_parse(&d, account_bytes, account_used, 4000000)) return 0;
    size_t accounts = cj_member(&d, 0, "accounts"); int ok = accounts != CJ_MISSING && d.tokens[accounts].kind == CJ_OBJECT;
    account_count = 0;
    for (size_t key = ok ? accounts + 1 : d.count; ok && key < d.tokens[accounts].next; key = d.tokens[d.tokens[key].next].next) {
        size_t record = d.tokens[key].next, account = cj_member(&d, record, "account");
        size_t disabled = cj_member(&d, record, "is_deactivated");
        if (disabled == CJ_MISSING) disabled = cj_member(&d, account, "is_deactivated");
        if (disabled != CJ_MISSING && d.tokens[disabled].kind == CJ_TRUE) continue;
        if (disabled != CJ_MISSING && d.tokens[disabled].kind != CJ_FALSE) { ok = 0; break; }
        if (account_count >= 64 || !cj_text(&d, cj_member(&d, account, "account_id"), account_ids[account_count], 257)
            || !safe_workspace(account_ids[account_count])) { ok = 0; break; }
        for (unsigned old = 0; old < account_count; ++old) if (!strcmp(account_ids[old], account_ids[account_count])) ok = 0;
        if (ok) ++account_count;
    }
    cj_free(&d); memset(account_bytes, 0, account_used); account_used = 0;
    if (!ok || !account_count) { account_count = 0; return 0; }
    jclass strings = (*env)->FindClass(env, "java/lang/String");
    jobjectArray labels = strings ? (*env)->NewObjectArray(env, (jsize)account_count, strings, NULL) : NULL;
    if (!labels) return 0;
    for (unsigned i = 0; i < account_count; ++i) {
        /* Workspace identifiers are already allowlisted ASCII. Show the actual
         * selected identity instead of guessing a personal/default workspace. */
        jstring label = (*env)->NewStringUTF(env, account_ids[i]);
        (*env)->SetObjectArrayElement(env, labels, (jsize)i, label);
        (*env)->DeleteLocalRef(env, label);
    }
    jclass adapter_class = (*env)->FindClass(env, "android/widget/ArrayAdapter");
    jmethodID init = adapter_class ? (*env)->GetMethodID(env, adapter_class, "<init>", "(Landroid/content/Context;I[Ljava/lang/Object;)V") : NULL;
    jobject adapter = init ? (*env)->NewObject(env, adapter_class, init, activity, 0x01090008, labels) : NULL;
    jmethodID set = method(env, workspace_view, "setAdapter", "(Landroid/widget/SpinnerAdapter;)V");
    if (!adapter || !set) return 0;
    (*env)->CallVoidMethod(env, workspace_view, set, adapter);
    return !(*env)->ExceptionCheck(env);
}
static int private_mode(int fd, int directory)
{
    struct stat state;
    return fd >= 0 && !fstat(fd, &state) && state.st_uid == geteuid() && !(state.st_mode & 077)
        && (directory ? S_ISDIR(state.st_mode) : S_ISREG(state.st_mode) && state.st_nlink == 1);
}
static int all_bytes(int fd, const unsigned char *bytes, size_t length)
{
    while (length) {
        ssize_t n = write(fd, bytes, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        bytes += n; length -= (size_t)n;
    }
    return 1;
}
static int select_root(void)
{
    if (!safe_workspace(selected_workspace)) return 0;
    cgw_sha256 hash; unsigned char digest[32]; char fingerprint[65];
    cgw_sha256_init(&hash);
    if (!cgw_sha256_update(&hash, selected_workspace, strlen(selected_workspace)) || !cgw_sha256_final(&hash, digest)) return 0;
    cgw_sha256_hex(digest, fingerprint);
    char parent[PATH_MAX]; (void)snprintf(parent, sizeof(parent), "%s", selection_file);
    char *slash = strrchr(parent, '/'); if (!slash) return 0; *slash = 0;
    int n = snprintf(archive_root, sizeof(archive_root), "%s/archive-%s", parent, fingerprint);
    if (n < 0 || (size_t)n >= sizeof(archive_root)) return 0;
    if (mkdir(archive_root, 0700) && errno != EEXIST) return 0;
    int fd = open(archive_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int valid = private_mode(fd, 1); if (fd >= 0) close(fd); return valid;
}
static int remember_selection(void)
{
    char temporary[PATH_MAX]; int n = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", selection_file);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return 0;
    int fd = mkstemp(temporary);
    if (fd < 0) return 0;
    int ok = !fchmod(fd, 0600) && all_bytes(fd, (const unsigned char *)selected_workspace, strlen(selected_workspace)) && !fsync(fd);
    if (close(fd)) ok = 0;
    if (ok && rename(temporary, selection_file)) ok = 0;
    if (!ok) (void)unlink(temporary);
    if (ok) {
        int root = open(archive_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        int member = root >= 0 ? openat(root, "workspace.txt", O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600) : -1;
        if (member >= 0) { ok = all_bytes(member, (const unsigned char *)selected_workspace, strlen(selected_workspace)) && !fsync(member); close(member); }
        else if (errno != EEXIST) ok = 0;
        else {
            member = openat(root, "workspace.txt", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            char prior[258] = {0}; ssize_t size = member >= 0 ? read(member, prior, 257) : -1;
            ok = private_mode(member, 0) && size == (ssize_t)strlen(selected_workspace) && !memcmp(prior, selected_workspace, (size_t)size);
            if (member >= 0) close(member);
        }
        if (root >= 0) { if (fsync(root)) ok = 0; close(root); }
    }
    return ok;
}
static int archive_initialize(JNIEnv *env)
{
    capture_stopped = capture_paused = capture_running = request_waiting = dispatch_pending = 0;
    worker_status[0] = selected_workspace[0] = archive_root[0] = 0;
    jmethodID get = method(env, activity, "getFilesDir", "()Ljava/io/File;");
    jobject files = get ? (*env)->CallObjectMethod(env, activity, get) : NULL;
    jmethodID absolute = method(env, files, "getAbsolutePath", "()Ljava/lang/String;");
    jstring value = absolute ? (*env)->CallObjectMethod(env, files, absolute) : NULL;
    const char *directory = value ? (*env)->GetStringUTFChars(env, value, NULL) : NULL;
    int n = directory ? snprintf(selection_file, sizeof(selection_file), "%s/selected-workspace.txt", directory) : -1;
    if (directory) (*env)->ReleaseStringUTFChars(env, value, directory);
    if (n < 0 || (size_t)n >= sizeof(selection_file)) return 0;
    int fd = open(selection_file, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t size = private_mode(fd, 0) ? read(fd, selected_workspace, 257) : -1; close(fd);
        if (size <= 0 || size > 256) selected_workspace[0] = 0;
        else { selected_workspace[size] = 0; if (!select_root()) selected_workspace[0] = 0; }
    }
    jclass looper_class = (*env)->FindClass(env, "android/os/Looper");
    jmethodID main = looper_class ? (*env)->GetStaticMethodID(env, looper_class, "getMainLooper", "()Landroid/os/Looper;") : NULL;
    jobject looper = main ? (*env)->CallStaticObjectMethod(env, looper_class, main) : NULL;
    jclass handler_class = (*env)->FindClass(env, "android/os/Handler");
    jmethodID init = handler_class ? (*env)->GetMethodID(env, handler_class, "<init>", "(Landroid/os/Looper;)V") : NULL;
    jobject handler = init && looper ? (*env)->NewObject(env, handler_class, init, looper) : NULL;
    jobject task = construct(env, "org/isomorphisms/webview/BridgeUiTask");
    if (!handler || !task) return 0;
    ui_handler = (*env)->NewGlobalRef(env, handler); ui_task = (*env)->NewGlobalRef(env, task);
    return ui_handler && ui_task && !(*env)->ExceptionCheck(env);
}
static int post_ui(void)
{
    jmethodID post_method = method(worker_env, ui_handler, "post", "(Ljava/lang/Runnable;)Z");
    int ok = post_method && (*worker_env)->CallBooleanMethod(worker_env, ui_handler, post_method, ui_task);
    if ((*worker_env)->ExceptionCheck(worker_env)) { (*worker_env)->ExceptionClear(worker_env); return 0; }
    return ok;
}
static cgw_webview_result archive_exchange(void *unused, const cgw_webview_command *command,
                                          cgw_webview_bridge *page_bridge, unsigned timeout_ms)
{
    (void)unused; (void)page_bridge;
    pending_command = *command; dispatch_pending = request_waiting = 1; request_result = CGW_WEBVIEW_NOT_READY;
    if (!post_ui()) { request_waiting = dispatch_pending = 0; return CGW_WEBVIEW_NOT_READY; }
    struct timespec deadline; clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    while (request_waiting && !capture_stopped) {
        if (pthread_cond_timedwait(&platform_changed, &platform_mutex, &deadline) == ETIMEDOUT) { request_result = CGW_WEBVIEW_NAVIGATION_CANCELLED; break; }
    }
    request_waiting = dispatch_pending = 0;
    if (capture_stopped) return CGW_WEBVIEW_NAVIGATION_CANCELLED;
    return request_result;
}
static cgw_fetch_result archive_fetch(void *unused, const cgw_input *input, cgw_fetch_options options,
                                     cgw_chunk_fn sink_fn, void *sink_context, cgw_fetch_observation *observation)
{
    (void)unused;
    pthread_mutex_lock(&platform_mutex);
    while (capture_paused && !capture_stopped) pthread_cond_wait(&platform_changed, &platform_mutex);
    if (capture_stopped) { memset(observation, 0, sizeof(*observation)); pthread_mutex_unlock(&platform_mutex); return CGW_FETCH_NETWORK_FAILURE; }
    cgw_webview_capture_transport transport = { bridge, selected_workspace, archive_exchange, NULL };
    cgw_fetch_result result = cgw_webview_capture_fetch(&transport, input, options, sink_fn, sink_context, observation);
    (void)snprintf(worker_status, sizeof(worker_status), "Capturing main and archived history - last response %zu bytes", observation->received_bytes);
    (void)post_ui(); pthread_mutex_unlock(&platform_mutex); return result;
}
static jobject create_document(JNIEnv *env, jobject parent, const char *mime, const char *name)
{
    jmethodID get = method(env, activity, "getContentResolver", "()Landroid/content/ContentResolver;");
    jobject resolver = get ? (*env)->CallObjectMethod(env, activity, get) : NULL;
    jclass cls = (*env)->FindClass(env, "android/provider/DocumentsContract");
    jmethodID create = cls ? (*env)->GetStaticMethodID(env, cls, "createDocument", "(Landroid/content/ContentResolver;Landroid/net/Uri;Ljava/lang/String;Ljava/lang/String;)Landroid/net/Uri;") : NULL;
    jstring type = (*env)->NewStringUTF(env, mime), label = (*env)->NewStringUTF(env, name);
    return create && resolver && type && label ? (*env)->CallStaticObjectMethod(env, cls, create, resolver, parent, type, label) : NULL;
}
static jobject document_descriptor(JNIEnv *env, jobject uri, const char *mode_text)
{
    jmethodID get = method(env, activity, "getContentResolver", "()Landroid/content/ContentResolver;");
    jobject resolver = get ? (*env)->CallObjectMethod(env, activity, get) : NULL;
    jmethodID open_method = method(env, resolver, "openFileDescriptor", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
    jstring mode_string = (*env)->NewStringUTF(env, mode_text);
    return open_method && mode_string ? (*env)->CallObjectMethod(env, resolver, open_method, uri, mode_string) : NULL;
}
static int descriptor_fd(JNIEnv *env, jobject descriptor)
{
    jmethodID get = method(env, descriptor, "getFd", "()I");
    return get ? (*env)->CallIntMethod(env, descriptor, get) : -1;
}
static void close_descriptor(JNIEnv *env, jobject descriptor)
{
    jmethodID close = method(env, descriptor, "close", "()V");
    if (close) (*env)->CallVoidMethod(env, descriptor, close);
}
static int copy_document(JNIEnv *env, int input, jobject uri)
{
    jobject output = document_descriptor(env, uri, "w"); int fd = descriptor_fd(env, output);
    unsigned char bytes[16384], expected[32], actual[32]; cgw_sha256 hash; cgw_sha256_init(&hash);
    int ok = fd >= 0;
    while (ok) {
        ssize_t count = read(input, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = 0; break; }
        if (!count) break;
        ok = all_bytes(fd, bytes, (size_t)count) && cgw_sha256_update(&hash, bytes, (size_t)count);
    }
    if (!cgw_sha256_final(&hash, expected)) ok = 0;
    close_descriptor(env, output);
    if ((*env)->ExceptionCheck(env)) return 0;
    jobject reopened = ok ? document_descriptor(env, uri, "r") : NULL;
    fd = descriptor_fd(env, reopened); if (fd < 0) ok = 0;
    cgw_sha256_init(&hash);
    while (ok) {
        ssize_t count = read(fd, bytes, sizeof(bytes));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ok = 0; break; }
        if (!count) break;
        if (!cgw_sha256_update(&hash, bytes, (size_t)count)) ok = 0;
    }
    if (!cgw_sha256_final(&hash, actual) || memcmp(expected, actual, 32)) ok = 0;
    close_descriptor(env, reopened);
    return ok && !(*env)->ExceptionCheck(env);
}
static int copy_directory(JNIEnv *env, int directory, jobject target, unsigned depth)
{
    if (depth > 4 || !private_mode(directory, 1)) return 0;
    DIR *entries = fdopendir(dup(directory)); if (!entries) return 0;
    struct dirent *entry; int ok = 1;
    while (ok) {
        errno = 0; entry = readdir(entries);
        if (!entry) { if (errno) ok = 0; break; }
        const char *name = entry->d_name;
        if (!strcmp(name, ".") || !strcmp(name, "..") || !strcmp(name, ".staging") || !strncmp(name, ".capture-stage-", 15) || !strncmp(name, "lock-", 5)) continue;
        pthread_mutex_lock(&platform_mutex); int stopped = capture_stopped; pthread_mutex_unlock(&platform_mutex);
        if (stopped || (*env)->PushLocalFrame(env, 32) < 0) { ok = 0; break; }
        int input = openat(directory, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC); struct stat state;
        if (input < 0 || fstat(input, &state)) ok = 0;
        else if (S_ISDIR(state.st_mode)) {
            jobject child = create_document(env, target, "vnd.android.document/directory", name);
            ok = child && copy_directory(env, input, child, depth + 1);
        } else if (private_mode(input, 0)) {
            jobject child = create_document(env, target, "application/octet-stream", name);
            ok = child && copy_document(env, input, child);
        } else ok = 0;
        if (input >= 0) close(input);
        if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); ok = 0; }
        (*env)->PopLocalFrame(env, NULL);
    }
    closedir(entries); return ok;
}
static cgw_capture_bounds android_bounds(void)
{
    cgw_capture_bounds bounds = cgw_capture_default_bounds();
    bounds.page_size = 100; bounds.max_conversations = 8192; bounds.max_requests = 16384;
    bounds.max_graph_nodes = 2048; bounds.max_response_bytes = 4194304;
    bounds.max_json_work = 16000000; bounds.max_received_bytes = UINT64_C(8589934592);
    return bounds;
}
static void *archive_work(void *unused)
{
    (void)unused;
    JNIEnv *env = NULL;
    if ((*java_vm)->AttachCurrentThread(java_vm, (void *)&env, NULL) != JNI_OK) {
        pthread_mutex_lock(&platform_mutex); capture_running = 0; pthread_mutex_unlock(&platform_mutex); return NULL;
    }
    worker_env = env; cgw_capture_report result;
    cgw_capture_result state = worker_action == 0
        ? cgw_capture_with_transport(archive_root, selected_workspace, archive_fetch, NULL, android_bounds(), &result)
        : cgw_capture_revalidate(archive_root, selected_workspace, android_bounds(), &result);
    int copied = 0;
    if (state == CGW_CAPTURE_COMPLETE && worker_action == 1) {
        jclass contract = (*env)->FindClass(env, "android/provider/DocumentsContract");
        jmethodID id = contract ? (*env)->GetStaticMethodID(env, contract, "getTreeDocumentId", "(Landroid/net/Uri;)Ljava/lang/String;") : NULL;
        jstring document_id = id ? (*env)->CallStaticObjectMethod(env, contract, id, export_tree) : NULL;
        jmethodID build_uri = contract ? (*env)->GetStaticMethodID(env, contract, "buildDocumentUriUsingTree", "(Landroid/net/Uri;Ljava/lang/String;)Landroid/net/Uri;") : NULL;
        jobject parent = build_uri && document_id ? (*env)->CallStaticObjectMethod(env, contract, build_uri, export_tree, document_id) : NULL;
        char label[80]; (void)snprintf(label, sizeof(label), "ChatGPTExport-%.12s-%lld", result.workspace_fingerprint, (long long)time(NULL));
        jobject destination = parent ? create_document(env, parent, "vnd.android.document/directory", label) : NULL;
        int root = open(archive_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        copied = destination && root >= 0 && copy_directory(env, root, destination, 0);
        if (root >= 0) close(root);
    }
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); state = CGW_CAPTURE_INCONCLUSIVE; }
    pthread_mutex_lock(&platform_mutex);
    capture_running = 0;
    if (!capture_stopped && state == CGW_CAPTURE_COMPLETE) {
        report(worker_action == 0 ? "archive:bounded_complete" : "archive:disk_verified");
        if (result.reused) report("archive:verified_restart_reuse");
        if (worker_action == 1) (void)snprintf(worker_status, sizeof(worker_status), "%s", copied ? "Archive copied; destination files reopened and byte-verified" : "Copy incomplete - destination may contain partial files; private archive retained");
        else (void)snprintf(worker_status, sizeof(worker_status), "%s %u main/archived conversations. Projects, shares and assets are not captured.", worker_action == 0 ? "Saved" : "Validated", result.completed);
    } else (void)snprintf(worker_status, sizeof(worker_status), "Capture/validation incomplete (%s). Saved evidence retained; check session and resume.", capture_stopped ? "cancelled or navigated" : result.reason);
    (void)post_ui(); pthread_mutex_unlock(&platform_mutex);
    worker_env = NULL; (*java_vm)->DetachCurrentThread(java_vm); return NULL;
}
static void archive_ui_run(JNIEnv *env)
{
    if (dispatch_pending && request_waiting) {
        dispatch_pending = 0;
        if (!capture_stopped && port_verified && current_document(env, webview)) post(env, active_port, pending_command.wire);
        else { request_result = CGW_WEBVIEW_NOT_READY; request_waiting = 0; pthread_cond_broadcast(&platform_changed); }
    }
    if (worker_status[0] && activity) { show_status(env, worker_status); worker_status[0] = 0; }
}
static void archive_start(JNIEnv *env, int action)
{
    if (capture_running) { show_status(env, "An archive operation is already running"); return; }
    if (worker_joinable) { pthread_join(archive_worker, NULL); worker_joinable = 0; }
    if (action == 0) {
        if (!port_verified || !account_count || !current_document(env, webview)) { show_status(env, "Sign in, then Check session before capture"); return; }
        jmethodID selected = method(env, workspace_view, "getSelectedItemPosition", "()I");
        jint index = selected ? (*env)->CallIntMethod(env, workspace_view, selected) : -1;
        if (index < 0 || (unsigned)index >= account_count) { show_status(env, "Select an accessible workspace"); return; }
        (void)snprintf(selected_workspace, sizeof(selected_workspace), "%s", account_ids[index]);
        if (!select_root() || !remember_selection()) { show_status(env, "Private archive storage refused"); return; }
    }
    if (!selected_workspace[0] || !archive_root[0]) { show_status(env, "No saved archive is selected"); return; }
    capture_stopped = capture_paused = 0; capture_running = 1; worker_action = action;
    show_status(env, action == 0 ? "Capturing main and archived history..." : action == 1 ? "Validating before copy..." : "Validating saved disk evidence...");
    if (pthread_create(&archive_worker, NULL, archive_work, NULL)) { capture_running = 0; show_status(env, "Could not start archive worker"); }
    else worker_joinable = 1;
}
static void archive_choose_destination(JNIEnv *env)
{
    if (capture_running || !archive_root[0]) { show_status(env, "Finish an archive operation before choosing a destination"); return; }
    jclass cls = (*env)->FindClass(env, "android/content/Intent");
    jmethodID init = cls ? (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V") : NULL;
    jstring action = (*env)->NewStringUTF(env, "android.intent.action.OPEN_DOCUMENT_TREE");
    jobject intent = init && action ? (*env)->NewObject(env, cls, init, action) : NULL;
    jmethodID launch = method(env, activity, "startActivityForResult", "(Landroid/content/Intent;I)V");
    if (intent && launch) (*env)->CallVoidMethod(env, activity, launch, intent, 71);
}
static void archive_activity_result(JNIEnv *env, jint request, jint result, jobject data)
{
    if (request != 71 || result != -1 || !data || capture_running) return;
    jmethodID get = method(env, data, "getData", "()Landroid/net/Uri;");
    jobject uri = get ? (*env)->CallObjectMethod(env, data, get) : NULL;
    if (!uri) { show_status(env, "No destination permission was granted"); return; }
    if (export_tree) (*env)->DeleteGlobalRef(env, export_tree);
    export_tree = (*env)->NewGlobalRef(env, uri); archive_start(env, 1);
}
static void archive_shutdown(JNIEnv *env)
{
    pthread_mutex_lock(&platform_mutex);
    capture_stopped = 1; capture_paused = 0; request_waiting = dispatch_pending = 0;
    if (bridge) (void)cgw_webview_bridge_cancel_request(bridge);
    pthread_cond_broadcast(&platform_changed);
    pthread_mutex_unlock(&platform_mutex);
    if (worker_joinable) { pthread_join(archive_worker, NULL); worker_joinable = 0; }
    if (ui_handler && ui_task) {
        jmethodID remove = method(env, ui_handler, "removeCallbacks", "(Ljava/lang/Runnable;)V");
        if (remove) (*env)->CallVoidMethod(env, ui_handler, remove, ui_task);
    }
}
