#ifndef REVERSI_AUDIO_AUDIO_H
#define REVERSI_AUDIO_AUDIO_H

#include <stdbool.h>

typedef enum AudioCue {
    AUDIO_CUE_PLACE = 0,
    AUDIO_CUE_FLIP,
    AUDIO_CUE_BUTTON,
    AUDIO_CUE_WIN,
    AUDIO_CUE_LOSE,
    AUDIO_CUE_DRAW,
    AUDIO_CUE_COUNT
} AudioCue;

/*
 * All audio is procedurally generated, so there are no asset files to go
 * missing. If no output device is available every function degrades to a
 * silent no-op.
 */
bool audio_init(int volume, bool enabled);
void audio_shutdown(void);
void audio_set_volume(int volume);
void audio_set_enabled(bool enabled);
bool audio_ready(void);
void audio_play(AudioCue cue);

#endif
