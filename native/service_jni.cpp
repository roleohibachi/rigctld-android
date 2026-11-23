// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"
#include "transport/usb_serial_bridge.hpp"

#include <jni.h>
#include <new>

namespace {
class TruSdxRoute {
public:
    explicit TruSdxRoute(embedded_audio* audio) : selected_(audio != nullptr) {
        if (selected_) js8core::android::usb_trusdx_select(audio);
    }
    ~TruSdxRoute() {
        if (selected_) js8core::android::usb_trusdx_select(nullptr);
    }

private:
    bool selected_;
};

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
    jstring baud, jstring handshake, jboolean allow_ptt, jboolean usb_audio_rx, jboolean trusdx_audio_rx, jlong output) {
    jclass type = env->GetObjectClass(service);
    jmethodID listening = env->GetMethodID(type, "onNativeListening", "(Z)V");
    if (listening == nullptr) return nullptr;
    try {
        const RigctldServer::Radio radio{
            static_cast<unsigned>(model), string_from_java(env, path),
            string_from_java(env, baud), string_from_java(env, handshake), allow_ptt == JNI_TRUE,
            false, usb_audio_rx == JNI_TRUE || trusdx_audio_rx == JNI_TRUE,
            false, reinterpret_cast<TxPlayback*>(output)
        };
        if (env->ExceptionCheck()) return nullptr;
        auto* server = server_from_handle(handle);
        TruSdxRoute route(trusdx_audio_rx ? server->audio() : nullptr);
        const auto error = server->run(4532, radio, [&] {
            bool started = !trusdx_audio_rx || js8core::android::usb_trusdx_start();
            env->CallVoidMethod(service, listening, static_cast<jboolean>(started));
            if (env->ExceptionCheck()) server->stop();
        });
        if (env->ExceptionCheck() || error.empty()) return nullptr;
        return env->NewStringUTF(error.c_str());
    } catch (const std::exception&) {
        return env->NewStringUTF("Native CAT server failed");
    }
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_rigctld_android_CatService_nativeTruSdxSamples(JNIEnv*, jclass) {
    return static_cast<jlong>(js8core::android::usb_trusdx_samples());
}

extern "C" JNIEXPORT void JNICALL
Java_org_rigctld_android_CatService_nativeAudioFailed(JNIEnv*, jclass, jlong handle) {
    embedded_audio_fail(server_from_handle(handle)->audio());
}

extern "C" JNIEXPORT void JNICALL
Java_org_rigctld_android_CatService_nativeStop(JNIEnv*, jclass, jlong handle) {
    server_from_handle(handle)->stop();
}

extern "C" JNIEXPORT void JNICALL
Java_org_rigctld_android_CatService_nativeDestroy(JNIEnv*, jclass, jlong handle) {
    delete server_from_handle(handle);
}
