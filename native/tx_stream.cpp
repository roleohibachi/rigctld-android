// SPDX-License-Identifier: GPL-3.0-or-later
#include "tx_stream.h"
#include "embedded_audio.h"

extern "C" {
#include "config.h"
#include "rigctld_client.h"
#include "stream.h"
}

#include <mutex>
#include <condition_variable>
#include <type_traits>

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

struct TxStream::State {
    // Hamlib calls the C hooks with only the rig handle. Use a per-instance
    // copy of the capabilities, leaving the global backend table untouched.
    struct HookTable {
        rig_caps caps;
        State* state;
    };
    static_assert(std::is_standard_layout_v<HookTable>);

    State(RIG* instance, TxPlayback& sink, embedded_audio* source)
        : rig(instance), playback(sink), capture(source), prior(instance->caps), table{*prior, this} {}

    static State& from(RIG* rig) {
        return *reinterpret_cast<HookTable*>(rig->caps)->state;
    }

    static int open(RIG* rig, rig_stream* stream) {
        auto& state = from(rig);
        const auto& config = stream->config;
        if (state.capture && config.type == RIG_STREAM_TYPE_AUDIO_RX) {
            return embedded_audio_open_rx(state.capture, stream);
        }
        const int client = rigctld_client_id_get();
        std::lock_guard lock(state.mutex);
        if (client <= 0 || state.stream || !state.playback.available()) return -RIG_ENAVAIL;
        if (config.type != RIG_STREAM_TYPE_AUDIO_TX || config.format != RIG_STREAM_FORMAT_PCM_S16
            || config.sample_rate != 48000 || config.channels != 1 || stream->conversions) {
            return -RIG_EINVAL;
        }
        state.stream = stream;
        state.owner = client;
        state.failed = false;
        return RIG_OK;
    }

    static int close(RIG* rig, rig_stream* stream) {
        auto& state = from(rig);
        if (state.capture && stream->type == RIG_STREAM_TYPE_AUDIO_RX) {
            embedded_audio_close_rx(state.capture, stream);
            return RIG_OK;
        }
        std::lock_guard lock(state.mutex);
        if (state.stream == stream) {
            state.playback.discard();
            state.enabled = false;
            state.stream = nullptr;
            state.owner = -1;
            state.changed.notify_all();
        }
        return RIG_OK;
    }

    static int write(RIG* rig, rig_stream* stream, const void* pcm, std::size_t bytes,
                     std::size_t* written, int, const rig_stream_write_info* info) {
        auto& state = from(rig);
        std::lock_guard lock(state.mutex);
        *written = 0;
        if (state.stream != stream || state.failed) return -RIG_EIO;
        // Pre-key packets and any packets after the burst boundary are discarded.
        if (state.enabled && !state.end_of_burst) {
            if ((info && (info->time_valid || (info->flags & ~RIG_STREAM_TIME_FLAG_EOB))) || bytes % 2 != 0
                || !state.playback.available() || (bytes && !state.playback.enqueue(pcm, bytes))) {
                state.playback.discard();
                state.enabled = false;
                state.failed = true;
                state.changed.notify_all();
                return -RIG_EIO;
            }
            if (bytes) state.last_pcm = Clock::now();
            if (info && (info->flags & RIG_STREAM_TIME_FLAG_EOB)) {
                state.end_of_burst = true;
                state.changed.notify_all();
            }
        }
        *written = bytes;
        return RIG_OK;
    }

    RIG* rig;
    TxPlayback& playback;
    embedded_audio* capture;
    rig_caps* prior;
    HookTable table;
    mutable std::mutex mutex;
    std::condition_variable changed;
    rig_stream* stream = nullptr;
    int owner = -1;
    bool enabled = false;
    bool failed = false;
    bool end_of_burst = false;
    Clock::time_point last_pcm;
};

TxStream::TxStream(RIG* rig, TxPlayback& playback, embedded_audio* capture)
    : state_(std::make_unique<State>(rig, playback, capture)) {}

TxStream::~TxStream() {
    if (state_->rig->caps == &state_->table.caps) state_->rig->caps = state_->prior;
}

int TxStream::attach() {
    auto& state = *state_;
    rig_stream_caps caps_list[2]{};
    auto& caps = caps_list[0];
    caps.type = RIG_STREAM_TYPE_AUDIO_TX;
    caps.formats = RIG_STREAM_FORMAT_PCM_S16;
    caps.sample_rates[0] = 48000;
    caps.channels[0] = 1;
    caps.max_streams = 1;
    if (state.capture) embedded_audio_rx_caps(&caps_list[1]);
    const int result = stream_set_session_caps(state.rig, caps_list, state.capture ? 2 : 1);
    if (result != RIG_OK) return result;
    state.table.caps.stream_open = State::open;
    state.table.caps.stream_close = State::close;
    state.table.caps.stream_write = State::write;
    if (state.capture) state.table.caps.stream_read = nullptr;
    state.rig->caps = &state.table.caps;
    return RIG_OK;
}

bool TxStream::canKey(int client) const {
    auto& state = *state_;
    std::lock_guard lock(state.mutex);
    return state.stream && state.owner == client && !state.failed && state.playback.available();
}

void TxStream::keyed(int client) {
    auto& state = *state_;
    std::lock_guard lock(state.mutex);
    if (!state.enabled) {
        state.last_pcm = Clock::now();
        state.end_of_burst = false;
    }
    state.enabled = state.stream && state.owner == client && !state.failed && state.playback.available();
}

void TxStream::finish(bool graceful) {
    auto& state = *state_;
    {
        std::unique_lock lock(state.mutex);
        if (graceful && state.enabled && !state.failed) {
            // TCP unkey can reach the server before the final UDP block.
            state.changed.wait_for(lock, 100ms, [&] {
                return state.end_of_burst || !state.enabled || state.failed;
            });
        }
        graceful &= state.enabled && !state.failed && state.end_of_burst;
        state.enabled = false;
        if (!graceful && state.stream) state.failed = true;
    }
    // The playback adapter owns its queue. Drain outside the ingress lock so
    // a UDP feeder can promptly discard late packets.
    if (!graceful || !state.playback.drain(200ms)) {
        state.playback.discard();
        std::lock_guard lock(state.mutex);
        if (state.stream) state.failed = true;
    }
}

bool TxStream::healthy() const {
    auto& state = *state_;
    std::lock_guard lock(state.mutex);
    return state.stream && state.enabled && !state.failed && state.playback.available()
        && Clock::now() - state.last_pcm < 700ms;
}
