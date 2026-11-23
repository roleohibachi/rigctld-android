// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct embedded_audio;
struct s_rig;
struct rig_stream;
struct rig_stream_caps;

struct embedded_audio *embedded_audio_create(void);
void embedded_audio_destroy(struct embedded_audio *audio);
int embedded_audio_attach(struct embedded_audio *audio, struct s_rig *rig);
void embedded_audio_push(struct embedded_audio *audio, const void *pcm, size_t size);
void embedded_audio_fail(struct embedded_audio *audio);
int embedded_audio_open_rx(struct embedded_audio *audio, struct rig_stream *stream);
void embedded_audio_close_rx(struct embedded_audio *audio, struct rig_stream *stream);
void embedded_audio_rx_caps(struct rig_stream_caps *caps);

#ifdef __cplusplus
}
#endif
