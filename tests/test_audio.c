#include "audio/audio.h"
#include "test_harness.h"

int main(void)
{
    TestStats stats = {0};

    /* Tests never open an audio device: every cue must be a safe no-op. */
    REVERSI_TEST_ASSERT(&stats, !audio_ready());
    for (int cue = 0; cue < AUDIO_CUE_COUNT; ++cue) {
        audio_play((AudioCue)cue);
    }
    audio_play((AudioCue)-1);
    audio_play((AudioCue)AUDIO_CUE_COUNT);
    audio_set_volume(-10);
    audio_set_volume(200);
    audio_set_enabled(false);
    audio_set_enabled(true);
    audio_shutdown();
    REVERSI_TEST_ASSERT(&stats, !audio_ready());

    return reversi_test_summary(&stats, "audio.noop");
}
