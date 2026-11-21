// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

#include <jni.h>
#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr int packets_per_transfer = 8;
constexpr int max_packet_size = 1024;

struct Transfer {
    std::unique_ptr<usbdevfs_urb, decltype(&std::free)> urb{
        static_cast<usbdevfs_urb*>(std::calloc(1, sizeof(usbdevfs_urb)
            + packets_per_transfer * sizeof(usbdevfs_iso_packet_desc))), std::free};
    std::array<unsigned char, packets_per_transfer * max_packet_size> bytes;
    bool pending = false;
};

int usb_ioctl(int fd, unsigned long request, void* argument) {
    int result;
    do {
        result = ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

struct TransferQueue {
    int fd;
    std::array<Transfer, 4> transfers;

    int submit(Transfer& transfer) const {
        if (usb_ioctl(fd, USBDEVFS_SUBMITURB, transfer.urb.get()) < 0) return errno;
        transfer.pending = true;
        return 0;
    }

    ~TransferQueue() {
        // DISCARDURB kills the pending transfer synchronously. Reap completion records
        // before Java resets and releases the interface on the same descriptor.
        for (auto& transfer : transfers) {
            if (transfer.pending) usb_ioctl(fd, USBDEVFS_DISCARDURB, transfer.urb.get());
        }
        void* completed;
        while (usb_ioctl(fd, USBDEVFS_REAPURBNDELAY, &completed) == 0) {}
    }
};

struct Levels {
    int frames = 0;
    int nonzero = 0;
    int peak = 0;
    double squares = 0;

    void add(const unsigned char* data, int byte_count, int channels) {
        frames += byte_count / (channels * 2);
        for (int offset = 0; offset < byte_count; offset += 2) {
            int sample = data[offset] | (data[offset + 1] << 8);
            if (sample >= 0x8000) sample -= 0x10000;
            nonzero += sample != 0;
            peak = std::max(peak, std::abs(sample));
            squares += static_cast<double>(sample) * sample;
        }
    }

    double rms(int channels) const {
        return frames == 0 ? 0 : std::sqrt(squares / (frames * channels));
    }
};

bool is_cancelled(JNIEnv* env, jobject cancelled, jmethodID get) {
    return env->CallBooleanMethod(cancelled, get) || env->ExceptionCheck();
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_rigctld_android_UsbAudioMonitor_nativeMeasure(
    JNIEnv* env, jclass, jint fd, jint endpoint, jint packet_size, jint channels,
    jobject cancelled, jobject update, jlong server_handle, jobject playback) {
    auto* server = reinterpret_cast<RigctldServer*>(server_handle);
    auto* audio = server ? server->audio() : nullptr;
    if (fd < 0 || packet_size <= 0 || packet_size > max_packet_size
        || (channels != 1 && channels != 2) || endpoint < 0x81 || endpoint > 0x8f
        || (server && (!audio || channels != 1))) {
        return env->NewStringUTF("Invalid USB capture parameters.");
    }
    jmethodID get = env->GetMethodID(env->GetObjectClass(cancelled), "get", "()Z");
    if (!get) return nullptr;
    jmethodID accept = nullptr;
    if (update) {
        accept = env->GetMethodID(env->GetObjectClass(update), "accept", "(Ljava/lang/Object;)V");
        if (!accept) return nullptr;
    }
    std::array<unsigned char, packets_per_transfer * max_packet_size> playback_buffer;
    jmethodID play = nullptr;
    jobject playback_samples = nullptr;
    if (playback) {
        play = env->GetMethodID(env->GetObjectClass(playback), "write", "(Ljava/nio/ByteBuffer;I)V");
        if (!play) return nullptr;
        playback_samples = env->NewDirectByteBuffer(playback_buffer.data(), playback_buffer.size());
        if (!playback_samples) return nullptr;
    }

    TransferQueue queue{fd, {}};
    int error = 0;
    const char* operation = "queue capture";
    for (auto& transfer : queue.transfers) {
        if (!transfer.urb) return env->NewStringUTF("Could not allocate USB transfer buffers.");
        auto& urb = *transfer.urb;
        urb.type = USBDEVFS_URB_TYPE_ISO;
        urb.endpoint = endpoint;
        urb.flags = USBDEVFS_URB_ISO_ASAP;
        urb.buffer = transfer.bytes.data();
        urb.buffer_length = packet_size * packets_per_transfer;
        urb.number_of_packets = packets_per_transfer;
        urb.usercontext = &transfer;
        for (int i = 0; i < packets_per_transfer; ++i) urb.iso_frame_desc[i].length = packet_size;
        error = queue.submit(transfer);
        if (error != 0) break;
    }

    Levels total;
    Levels recent;
    int packets = 0;
    int packet_errors = 0;
    const auto start = Clock::now();
    const auto end = audio ? Clock::time_point::max() : start + 5s;
    auto last_frames = start;
    auto next_update = start + 250ms;
    bool reported_ready = false;
    char message[256];
    while (error == 0 && Clock::now() < end && !is_cancelled(env, cancelled, get)) {
        if (audio && Clock::now() - last_frames > 2s) {
            operation = "receive frames";
            error = ETIMEDOUT;
            break;
        }
        pollfd waiter{fd, POLLOUT, 0};
        int ready = poll(&waiter, 1, 100);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        operation = "wait for capture";
        if (ready < 0) {
            error = errno;
            break;
        }
        usbdevfs_urb* completed = nullptr;
        operation = "read capture";
        if (usb_ioctl(fd, USBDEVFS_REAPURBNDELAY, &completed) < 0) {
            if (errno == EAGAIN && !(waiter.revents & (POLLERR | POLLHUP | POLLNVAL))) continue;
            error = errno == EAGAIN ? ENODEV : errno;
            break;
        }
        auto& transfer = *static_cast<Transfer*>(completed->usercontext);
        transfer.pending = false;
        if (completed->status != 0) {
            error = -completed->status;
            break;
        }
        int playback_bytes = 0;
        for (int i = 0; i < packets_per_transfer; ++i) {
            const auto& packet = completed->iso_frame_desc[i];
            if (!audio) ++packets;
            if (packet.status != 0 || packet.actual_length > static_cast<unsigned>(packet_size)
                || packet.actual_length % (channels * 2) != 0) {
                if (!audio) ++packet_errors;
                continue;
            }
            const auto* data = transfer.bytes.data() + i * packet_size;
            if (audio && packet.actual_length > 0) {
                embedded_audio_push(audio, data, packet.actual_length);
                last_frames = Clock::now();
                if (playback) {
                    std::memcpy(playback_buffer.data() + playback_bytes, data, packet.actual_length);
                    playback_bytes += packet.actual_length;
                }
            } else if (!audio) {
                total.add(data, packet.actual_length, channels);
                recent.add(data, packet.actual_length, channels);
            }
        }
        if (playback_bytes > 0) {
            env->CallVoidMethod(playback, play, playback_samples, playback_bytes);
            if (env->ExceptionCheck()) return nullptr;
        }
        if (update && ((audio && !reported_ready && last_frames != start)
                       || (!audio && Clock::now() >= next_update))) {
            if (audio) {
                std::snprintf(message, sizeof(message), "C-Media USB receive ready · 48 kHz mono PCM16");
                reported_ready = true;
            } else {
                std::snprintf(message, sizeof(message), "%d frames | Current peak %d | RMS %.0f",
                    total.frames, recent.peak, recent.rms(channels));
            }
            jstring progress = env->NewStringUTF(message);
            if (!progress) return nullptr;
            env->CallVoidMethod(update, accept, progress);
            env->DeleteLocalRef(progress);
            if (env->ExceptionCheck()) return nullptr;
            recent = {};
            next_update += 250ms;
        }
        operation = "queue capture";
        error = queue.submit(transfer);
    }
    if (env->ExceptionCheck()) return nullptr;
    if (!audio && error == 0 && total.frames == 0 && !is_cancelled(env, cancelled, get)) {
        operation = "receive frames";
        error = ETIMEDOUT;
    }
    char status[80];
    if (error != 0) {
        std::snprintf(status, sizeof(status), "Could not %s (USB error %d).", operation, error);
    } else if (is_cancelled(env, cancelled, get)) {
        std::snprintf(status, sizeof(status), "Monitor stopped.");
    } else {
        std::snprintf(status, sizeof(status), "%s", total.nonzero == 0 ? "All samples were zero." : "Samples received.");
    }
    if (env->ExceptionCheck()) return nullptr;
    if (audio) return env->NewStringUTF(status);
    std::snprintf(message, sizeof(message),
        "%s\n48 kHz / %d ch / PCM16 | Frames %d | Nonzero samples %d\nPeak %d | RMS %.0f | USB packet errors %d/%d",
        status, channels, total.frames, total.nonzero, total.peak, total.rms(channels), packet_errors, packets);
    return env->NewStringUTF(message);
}
