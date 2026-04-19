#include "audio.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "driver/dac_continuous.h"
#include "esp_err.h"
#include "esp_log.h"

#define AUDIO_TAG "AUDIO"

// HFP mSBC decoded stream is 16 kHz PCM (state=CONNECTED_MSBC).
#define SAMPLE_RATE 16000

// Ring buffer: ~500 ms of 8-bit samples = 4096 bytes
#define RINGBUF_SIZE 4096
#define AUDIO_TASK_STACK_BYTES 4096
// DAC DMA descriptor turnaround is typically >10 ms at 8 kHz on ESP32.
// Keep this comfortably above that to avoid descriptor timeout spam.
#define AUDIO_WRITE_TIMEOUT_MS 100

static dac_continuous_handle_t dac_handle = NULL;
static RingbufHandle_t rb_handle = NULL;
static TaskHandle_t audio_task_handle = NULL;
static SemaphoreHandle_t dac_mutex = NULL;
static volatile bool audio_running = false;

static void audio_enqueue_dac_u8(const uint8_t *data, size_t len)
{
    if (!rb_handle || !data || len == 0) {
        return;
    }

    // Non-blocking enqueue; drop if full.
    xRingbufferSend(rb_handle, data, len, 0);
}

static void audio_drain_ringbuf(void)
{
    if (!rb_handle) {
        return;
    }

    while (true) {
        size_t item_size = 0;
        uint8_t *item = xRingbufferReceiveUpTo(rb_handle, &item_size, 0, RINGBUF_SIZE);
        if (!item) {
            break;
        }
        vRingbufferReturnItem(rb_handle, item);
    }
}

void audio_enqueue_output_u8(const uint8_t *data, size_t len)
{
    audio_enqueue_dac_u8(data, len);
}

// Dedicated task that drains the ring buffer into the DAC.
// Runs on its own stack so blocking dac_continuous_write() can't starve BTU_TASK.
static void audio_output_task(void *arg)
{
    uint8_t dac_buf[240];

    while (true) {
        if (!audio_running || !rb_handle || !dac_handle) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        size_t item_size = 0;
        uint8_t *item = xRingbufferReceiveUpTo(rb_handle, &item_size, pdMS_TO_TICKS(20), sizeof(dac_buf));
        if (item && item_size > 0) {
            if (item_size > sizeof(dac_buf)) {
                item_size = sizeof(dac_buf);
            }
            memcpy(dac_buf, item, item_size);
            vRingbufferReturnItem(rb_handle, item);

            if (!audio_running) {
                continue;
            }

            if (!dac_mutex || xSemaphoreTake(dac_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
                continue;
            }

            size_t loaded = 0;
            esp_err_t err = dac_continuous_write(dac_handle, dac_buf, item_size, &loaded, AUDIO_WRITE_TIMEOUT_MS);
            xSemaphoreGive(dac_mutex);

            if (err == ESP_ERR_TIMEOUT) {
                // Back off briefly so we don't spin and flood driver timeout logs.
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }

            if (err != ESP_OK && err != ESP_ERR_TIMEOUT && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(AUDIO_TAG, "dac_continuous_write failed: %s", esp_err_to_name(err));
            }
        }
    }
}

void audio_init(void)
{
    rb_handle = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!rb_handle) {
        ESP_LOGE(AUDIO_TAG, "Failed to create ring buffer");
        return;
    }

    dac_mutex = xSemaphoreCreateMutex();
    if (!dac_mutex) {
        ESP_LOGE(AUDIO_TAG, "Failed to create DAC mutex");
        return;
    }

    dac_continuous_config_t dac_cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH1,  // GPIO26
        .desc_num = 8,
        .buf_size = 256,
        .freq_hz = SAMPLE_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };

    esp_err_t err = dac_continuous_new_channels(&dac_cfg, &dac_handle);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "dac_continuous_new_channels failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_ok = xTaskCreate(audio_output_task, "audio_out", AUDIO_TASK_STACK_BYTES, NULL, 5, &audio_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(AUDIO_TAG, "Failed to create audio task");
        return;
    }

    ESP_LOGI(AUDIO_TAG, "Audio initialised (DAC CH1 / GPIO26, %d Hz)", SAMPLE_RATE);
}

void audio_start(void)
{
    if (audio_running || !dac_handle) {
        return;
    }

    if (!dac_mutex || xSemaphoreTake(dac_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(AUDIO_TAG, "Failed to lock DAC for start");
        return;
    }

    esp_err_t err = dac_continuous_enable(dac_handle);
    xSemaphoreGive(dac_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "dac_continuous_enable failed: %s", esp_err_to_name(err));
        return;
    }

    audio_drain_ringbuf();

    audio_running = true;
    ESP_LOGI(AUDIO_TAG, "Audio output started");
}

void audio_stop(void)
{
    if (!audio_running || !dac_handle) {
        return;
    }

    audio_running = false;
    audio_drain_ringbuf();

    if (!dac_mutex || xSemaphoreTake(dac_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(AUDIO_TAG, "Failed to lock DAC for stop");
        return;
    }

    esp_err_t err = dac_continuous_disable(dac_handle);
    xSemaphoreGive(dac_mutex);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(AUDIO_TAG, "dac_continuous_disable failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(AUDIO_TAG, "Audio output stopped");
}

void audio_receive(const uint8_t *data, uint32_t len)
{
    if (!audio_running || !rb_handle || !data || len == 0) {
        return;
    }

    // HFP sends 16-bit signed PCM (little-endian).
    // Convert to 8-bit unsigned for the DAC: take the high byte and shift from
    // signed [-128..127] to unsigned [0..255].
    uint32_t sample_count = len / 2;
    uint8_t conv_buf[240];
    uint32_t pos = 0;

    while (pos < sample_count) {
        uint32_t chunk = sample_count - pos;
        if (chunk > sizeof(conv_buf)) {
            chunk = sizeof(conv_buf);
        }

        for (uint32_t i = 0; i < chunk; i++) {
            int8_t high = (int8_t)data[(pos + i) * 2 + 1];
            conv_buf[i] = (uint8_t)(high + 128);
        }

        audio_enqueue_dac_u8(conv_buf, chunk);

        pos += chunk;
    }
}

uint32_t audio_send(uint8_t *buf, uint32_t len)
{
    // No microphone connected yet — send silence so the SCO link stays happy
    memset(buf, 0, len);
    return len;
}
