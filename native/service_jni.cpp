// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

#include <jni.h>
#include <new>

namespace {
RigctldServer* server_from_handle(jlong handle) {
    return reinterpret_cast<RigctldServer*>(handle);
}

std::string string_from_java(JNIEnv* env, jstring value) {
    if (value == nullptr) return {};
    const char* text = env->GetStringUTFChars(value, nullptr);
    if (text == nullptr) return {};
    std::string result(text);
    env->ReleaseStringUTFChars(value, text);
    return result;
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_rigctld_android_CatService_nativeCreate(JNIEnv*, jclass) {
    return reinterpret_cast<jlong>(new (std::nothrow) RigctldServer);
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_rigctld_android_CatService_nativeRun(
    JNIEnv* env, jobject service, jlong handle, jint model, jstring path,
    jstring baud, jstring handshake, jboolean allow_ptt) {
    jclass type = env->GetObjectClass(service);
    jmethodID listening = env->GetMethodID(type, "onNativeListening", "()V");
    if (listening == nullptr) return nullptr;
    try {
        const RigctldServer::Radio radio{
            static_cast<unsigned>(model), string_from_java(env, path),
            string_from_java(env, baud), string_from_java(env, handshake), allow_ptt == JNI_TRUE
        };
        if (env->ExceptionCheck()) return nullptr;
        const auto error = server_from_handle(handle)->run(4532, radio, [&] {
            env->CallVoidMethod(service, listening);
            if (env->ExceptionCheck()) server_from_handle(handle)->stop();
        });
        if (env->ExceptionCheck() || error.empty()) return nullptr;
        return env->NewStringUTF(error.c_str());
    } catch (const std::exception&) {
        return env->NewStringUTF("Native CAT server failed");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_rigctld_android_CatService_nativeStop(JNIEnv*, jclass, jlong handle) {
    server_from_handle(handle)->stop();
}

extern "C" JNIEXPORT void JNICALL
Java_org_rigctld_android_CatService_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete server_from_handle(handle);
}
