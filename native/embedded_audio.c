// SPDX-License-Identifier: GPL-3.0-or-later
#include "embedded_audio.h"
#include "config.h"
#include "stream.h"

#include <stdlib.h>

struct embedded_audio {
    pthread_mutex_t lock;
    struct rig_stream *stream;
    int receiving;
};

struct embedded_audio *embedded_audio_create(void)
{
    struct embedded_audio *audio = calloc(1, sizeof(*audio));
    if (audio && pthread_mutex_init(&audio->lock, NULL) != 0) {
        free(audio);
        return NULL;
    }
    return audio;
}

void embedded_audio_destroy(struct embedded_audio *audio)
{
    if (!audio) return;
    pthread_mutex_destroy(&audio->lock);
    free(audio);
}

int embedded_audio_open_rx(struct embedded_audio *audio, struct rig_stream *stream)
{
    pthread_mutex_lock(&audio->lock);
    int result = -RIG_ENAVAIL;
    if (audio->receiving && !audio->stream) {
        audio->stream = stream;
        result = RIG_OK;
    }
    pthread_mutex_unlock(&audio->lock);
    return result;
}

void embedded_audio_close_rx(struct embedded_audio *audio, struct rig_stream *stream)
{
    pthread_mutex_lock(&audio->lock);
    if (audio->stream == stream) audio->stream = NULL;
    pthread_mutex_unlock(&audio->lock);
}

static int open_audio(void *context, struct rig_stream *stream)
{
    return embedded_audio_open_rx(context, stream);
}

static void close_audio(void *context, struct rig_stream *stream)
{
    embedded_audio_close_rx(context, stream);
}

void embedded_audio_rx_caps(struct rig_stream_caps *caps)
{
    *caps = (struct rig_stream_caps) {
        .type = RIG_STREAM_TYPE_AUDIO_RX,
        .formats = RIG_STREAM_FORMAT_PCM_S16,
        .sample_rates = { 48000 },
        .channels = { 1 },
        .max_streams = 1,
    };
}

int embedded_audio_attach(struct embedded_audio *audio, RIG *rig)
{
    const struct stream_rx_provider provider = { audio, open_audio, close_audio };
    struct rig_stream_caps caps;
    embedded_audio_rx_caps(&caps);
    return audio ? stream_set_rx_provider(rig, &provider, &caps) : -RIG_ENOMEM;
}

void embedded_audio_push(struct embedded_audio *audio, const void *pcm, size_t size)
{
    if (!audio || size == 0) return;
    pthread_mutex_lock(&audio->lock);
    audio->receiving = 1;
    if (audio->stream && !audio->stream->paused) stream_backend_write(audio->stream, pcm, size);
    pthread_mutex_unlock(&audio->lock);
}

void embedded_audio_fail(struct embedded_audio *audio)
{
    if (!audio) return;
    pthread_mutex_lock(&audio->lock);
    audio->receiving = 0;
    if (audio->stream) stream_mark_failed(audio->stream, RIG_COMM_REASON_PEER_DISCONNECT);
    pthread_mutex_unlock(&audio->lock);
}
