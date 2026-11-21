// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct embedded_audio;
struct s_rig;

struct embedded_audio *embedded_audio_create(void);
void embedded_audio_destroy(struct embedded_audio *audio);
int embedded_audio_attach(struct embedded_audio *audio, struct s_rig *rig);
void embedded_audio_push(struct embedded_audio *audio, const void *pcm, size_t size);
void embedded_audio_fail(struct embedded_audio *audio);

#ifdef __cplusplus
}
#endif
