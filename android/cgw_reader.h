#ifndef CGW_ANDROID_READER_H
#define CGW_ANDROID_READER_H
#include <jni.h>
#include "cgw_capture.h"
/* UI-thread entry points. The worker owns verified, immutable C1 snapshots;
 * no bridge, credentials, transport or persistent presentation store. */
int cgw_reader_open(JNIEnv *, jobject activity, const char *root,
    const char *workspace, cgw_capture_bounds);
int cgw_reader_active(void);
/* 1 handled; -1 explicit Close; 0 not a reader control. */
int cgw_reader_click(JNIEnv *, jobject view);
void cgw_reader_close(JNIEnv *);
#endif
