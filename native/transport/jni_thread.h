// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <jni.h>

inline JNIEnv* transport_env(JavaVM* vm) {
    if (!vm) return nullptr;
    JNIEnv* env = nullptr;
    const jint status = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) return env;
    if (status != JNI_EDETACHED || vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return nullptr;
    }

    struct Attachment {
        JavaVM* vm;
        ~Attachment() { vm->DetachCurrentThread(); }
    };
    thread_local Attachment attachment{vm};
    return env;
}
