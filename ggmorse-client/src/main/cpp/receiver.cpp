// SPDX-License-Identifier: GPL-3.0-or-later
#include <ggmorse/ggmorse.h>
#include <hamlib/rig.h>
#include <jni.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>

namespace {
constexpr int sample_rate = 48000;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::string error_message(const char* operation, int error) {
    return std::string(operation) + ": " + rigerror(error);
}

void notify(JNIEnv* env, jobject activity, jmethodID method, const char* message) {
    jstring text = env->NewStringUTF(message);
    if (!text) return;
    env->CallVoidMethod(activity, method, text);
    env->DeleteLocalRef(text);
}

std::string run_receiver(JNIEnv* env, jobject activity, jobject stop) {
    jclass activity_type = env->GetObjectClass(activity);
    jmethodID status = env->GetMethodID(activity_type, "onReceiveStatus", "(Ljava/lang/String;)V");
    if (!status) return {};
    jmethodID decoded = env->GetMethodID(activity_type, "onDecodedText", "(Ljava/lang/String;)V");
    if (!decoded) return {};
    jmethodID get = env->GetMethodID(env->GetObjectClass(stop), "get", "()Z");
    if (!get) return {};
    const auto cancelled = [&] { return env->ExceptionCheck() || env->CallBooleanMethod(stop, get); };

    std::unique_ptr<RIG, decltype(&rig_cleanup)> rig(rig_init(RIG_MODEL_NETRIGCTL), rig_cleanup);
    if (!rig) return "Could not create the Hamlib client";
    for (const auto& [key, value] : {std::pair{"rig_pathname", "127.0.0.1:4532"},
                                     std::pair{"timeout", "1000"}, std::pair{"retry", "0"}}) {
        int result = rig_set_conf(rig.get(), rig_token_lookup(rig.get(), key), value);
        if (result != RIG_OK) return error_message("Configure rigctld", result);
    }

    int result = rig_open(rig.get());
    if (result != RIG_OK) return error_message("Connect to rigctld", result);
    if (cancelled()) return {};
    std::unique_ptr<rig_stream_config, decltype(&rig_stream_config_free)> config(
        rig_stream_config_alloc(), rig_stream_config_free);
    if (!config) return "Could not allocate the Hamlib stream configuration";
    config->type = RIG_STREAM_TYPE_AUDIO_RX;
    config->format = RIG_STREAM_FORMAT_PCM_S16;
    config->sample_rate = sample_rate;
    config->channels = 1;
    rig_stream_t* handle = nullptr;
    result = rig_stream_open(rig.get(), config.get(), &handle);
    if (result != RIG_OK) return error_message("Open rigctld receive audio", result);
    auto close_stream = [&](rig_stream_t* s) { rig_stream_close(rig.get(), s); };
    std::unique_ptr<rig_stream_t, decltype(close_stream)> stream(handle, close_stream);

    auto settings = GGMorse::getDefaultParameters();
    settings.sampleRateInp = sample_rate;
    settings.sampleFormatInp = GGMORSE_SAMPLE_FORMAT_I16;
    auto decoder = std::make_unique<GGMorse>(settings);
    GGMorse::TxRx text;
    notify(env, activity, status, "Listening · 48 kHz mono · 127.0.0.1:4532");
    auto last_audio = Clock::now();
    auto next_level = last_audio + 1s;
    int peak = 0;

    while (!cancelled()) {
        bool supplied = false;
        bool gap = false;
        int read_error = RIG_OK;
        decoder->decode([&](void* destination, uint32_t wanted) -> uint32_t {
            if (supplied) return 0;
            auto* bytes = static_cast<uint8_t*>(destination);
            size_t total = 0;
            while (total < wanted && !cancelled()) {
                size_t received = 0;
                rig_stream_read_info info{};
                const int read = rig_stream_read(rig.get(), stream.get(), bytes + total,
                    wanted - total, &received, 250, &info);
                if (read != RIG_OK && read != -RIG_ETIMEOUT) {
                    read_error = read;
                    return 0;
                }
                if (received == 0) {
                    if (Clock::now() - last_audio >= 3s) {
                        read_error = -RIG_ETIMEOUT;
                        return 0;
                    }
                    continue;
                }
                last_audio = Clock::now();
                if (info.dropped_samples || info.drop_flags) {
                    gap = true;
                    return 0;
                }
                total += received;
            }
            if (total != wanted) return 0;
            for (size_t offset = 0; offset < total; offset += 2) {
                int sample = bytes[offset] | (bytes[offset + 1] << 8);
                if (sample >= 32768) sample -= 65536;
                peak = std::max(peak, std::abs(sample));
            }
            supplied = true;
            return wanted;
        });
        if (read_error != RIG_OK) return error_message("Read rigctld audio", read_error);
        if (decoder->takeRxData(text) > 0) {
            const std::string transcript(text.begin(), text.end());
            notify(env, activity, decoded, transcript.c_str());
        }
        if (gap) decoder = std::make_unique<GGMorse>(settings);
        if (Clock::now() >= next_level) {
            char message[128];
            if (peak > 0) {
                std::snprintf(message, sizeof(message), "Listening · 48 kHz mono · peak %.0f dBFS",
                    20 * std::log10(peak / 32768.0));
            } else {
                std::snprintf(message, sizeof(message), "Listening · 48 kHz mono · silent input");
            }
            notify(env, activity, status, message);
            peak = 0;
            next_level = Clock::now() + 1s;
        }
    }
    return {};
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_rigctld_ggmorse_MainActivity_receive(JNIEnv* env, jobject activity, jobject stop) {
    try {
        const auto result = run_receiver(env, activity, stop);
        return env->ExceptionCheck() ? nullptr : env->NewStringUTF(result.c_str());
    } catch (const std::exception& failure) {
        return env->NewStringUTF(failure.what());
    }
}
