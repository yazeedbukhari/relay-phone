#include "phone.h"
#include <stdbool.h>
#include <stdint.h>
#include "audio.h"
#include "gap.h"
#include "esp_log.h"

#define PHONE_TAG "PHONE"
#define RING_FREQ_HZ 1000
#define RING_SAMPLE_RATE_HZ 16000
#define RING_TICK_SAMPLES (RING_SAMPLE_RATE_HZ / 10) // app_main loop is 100 ms
#define RING_LEVEL_HIGH 176
#define RING_LEVEL_LOW 80

static bool ringtone_active = false;
static uint32_t ring_phase = 0;

void phone_play_ringtone_tick(void)
{
    if (!ringtone_active) {
        audio_start();
        ring_phase = 0;
        ESP_LOGI(PHONE_TAG, "Ringtone start");
        ringtone_active = true;
    }

    uint8_t tone_buf[240];
    uint32_t remaining = RING_TICK_SAMPLES;
    const uint32_t period = (RING_SAMPLE_RATE_HZ / RING_FREQ_HZ);
    const uint32_t half_period = period / 2;

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > sizeof(tone_buf)) {
            chunk = sizeof(tone_buf);
        }

        for (uint32_t i = 0; i < chunk; i++) {
            uint32_t pos = ring_phase % period;
            tone_buf[i] = (pos < half_period) ? RING_LEVEL_HIGH : RING_LEVEL_LOW;
            ring_phase++;
        }

        audio_enqueue_dac_u8(tone_buf, chunk);
        remaining -= chunk;
    }
}

void phone_stop_ringtone(void)
{
    if (!ringtone_active) {
        return;
    }

    if (!gap_is_audio_active()) {
        audio_stop();
    }

    ESP_LOGI(PHONE_TAG, "Ringtone stop");
    ringtone_active = false;
}
