#include "audio/audio.h"

#include <raylib.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define REVERSI_AUDIO_SAMPLE_RATE 44100u
#define REVERSI_AUDIO_PI 3.14159265358979323846

static Sound sounds[AUDIO_CUE_COUNT];
static bool loaded[AUDIO_CUE_COUNT];
static bool ready;
static bool enabled = true;
static int current_volume = 70;

static short *allocate_samples(unsigned int count)
{
    return calloc(count, sizeof(short));
}

static Sound synth_tone(float start_hz, float end_hz, float seconds,
                        float gain)
{
    const unsigned int count =
        (unsigned int)(seconds * (float)REVERSI_AUDIO_SAMPLE_RATE);
    short *samples = allocate_samples(count);
    if (samples == NULL) {
        return (Sound){0};
    }

    double phase = 0.0;
    for (unsigned int index = 0; index < count; ++index) {
        const float t = count > 1 ? (float)index / (float)(count - 1) : 0.0f;
        const float frequency = start_hz + (end_hz - start_hz) * t;
        phase += 2.0 * REVERSI_AUDIO_PI * (double)frequency
            / (double)REVERSI_AUDIO_SAMPLE_RATE;
        const float attack = fminf(1.0f, t / 0.08f);
        const float decay = (1.0f - t) * (1.0f - t);
        const float sample = sinf((float)phase) * gain * attack * decay;
        samples[index] = (short)(sample * 32767.0f);
    }

    Wave wave = {
        .frameCount = count,
        .sampleRate = REVERSI_AUDIO_SAMPLE_RATE,
        .sampleSize = 16,
        .channels = 1,
        .data = samples
    };
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

static Sound synth_notes(const float *notes, int count, float note_seconds,
                         float gain)
{
    const unsigned int note_samples =
        (unsigned int)(note_seconds * (float)REVERSI_AUDIO_SAMPLE_RATE);
    const unsigned int total = note_samples * (unsigned int)count;
    short *samples = allocate_samples(total);
    if (samples == NULL) {
        return (Sound){0};
    }

    unsigned int cursor = 0;
    for (int note = 0; note < count; ++note) {
        double phase = 0.0;
        for (unsigned int index = 0; index < note_samples; ++index) {
            const float t = (float)index / (float)note_samples;
            phase += 2.0 * REVERSI_AUDIO_PI * (double)notes[note]
                / (double)REVERSI_AUDIO_SAMPLE_RATE;
            const float attack = fminf(1.0f, t / 0.06f);
            const float decay = (1.0f - t) * (1.0f - t);
            const float sample = sinf((float)phase) * gain * attack * decay;
            samples[cursor++] = (short)(sample * 32767.0f);
        }
    }

    Wave wave = {
        .frameCount = total,
        .sampleRate = REVERSI_AUDIO_SAMPLE_RATE,
        .sampleSize = 16,
        .channels = 1,
        .data = samples
    };
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

static bool sound_valid(Sound sound)
{
    return sound.frameCount > 0 && sound.stream.buffer != NULL;
}

static void unload_all(void)
{
    for (int index = 0; index < AUDIO_CUE_COUNT; ++index) {
        if (loaded[index]) {
            UnloadSound(sounds[index]);
            loaded[index] = false;
        }
        sounds[index] = (Sound){0};
    }
}

bool audio_init(int volume, bool sfx_enabled)
{
    audio_shutdown();
    current_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
    enabled = sfx_enabled;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        return false;
    }

    const float win_notes[] = {523.25f, 659.25f, 783.99f, 1046.50f};
    const float lose_notes[] = {392.00f, 329.63f, 261.63f};
    const float draw_notes[] = {440.00f, 440.00f};

    sounds[AUDIO_CUE_PLACE] = synth_tone(210.0f, 150.0f, 0.075f, 0.20f);
    sounds[AUDIO_CUE_FLIP] = synth_tone(430.0f, 300.0f, 0.055f, 0.11f);
    sounds[AUDIO_CUE_BUTTON] = synth_tone(640.0f, 600.0f, 0.038f, 0.09f);
    sounds[AUDIO_CUE_WIN] = synth_notes(win_notes, 4, 0.095f, 0.13f);
    sounds[AUDIO_CUE_LOSE] = synth_notes(lose_notes, 3, 0.115f, 0.13f);
    sounds[AUDIO_CUE_DRAW] = synth_notes(draw_notes, 2, 0.100f, 0.11f);

    for (int index = 0; index < AUDIO_CUE_COUNT; ++index) {
        if (!sound_valid(sounds[index])) {
            unload_all();
            CloseAudioDevice();
            ready = false;
            return false;
        }
        loaded[index] = true;
    }

    ready = true;
    return true;
}

void audio_shutdown(void)
{
    if (!ready) {
        return;
    }
    unload_all();
    CloseAudioDevice();
    ready = false;
}

void audio_set_volume(int volume)
{
    current_volume = volume < 0 ? 0 : (volume > 100 ? 100 : volume);
}

void audio_set_enabled(bool sfx_enabled)
{
    enabled = sfx_enabled;
}

bool audio_ready(void)
{
    return ready;
}

void audio_play(AudioCue cue)
{
    if (!ready || !enabled || current_volume <= 0
        || cue < 0 || cue >= AUDIO_CUE_COUNT || !loaded[cue]) {
        return;
    }
    SetSoundVolume(sounds[cue], (float)current_volume / 100.0f);
    PlaySound(sounds[cue]);
}
