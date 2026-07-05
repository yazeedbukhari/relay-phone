#include "audio.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "driver/dac_continuous.h"
#include "esp_adc/adc_oneshot.h"
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
#define MIC_ADC_UNIT ADC_UNIT_1
#define MIC_ADC_CHANNEL ADC_CHANNEL_6  // GPIO34
#define MIC_ADC_CENTER 2048
#define MIC_GAIN_SHIFT 4
#define MIC_LEVEL_LOG_INTERVAL_MS 1000

static dac_continuous_handle_t dac_handle = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static RingbufHandle_t rb_handle = NULL;
static TaskHandle_t audio_task_handle = NULL;
static SemaphoreHandle_t dac_mutex = NULL;
static volatile bool audio_running = false;

static uint32_t mic_log_last_ms = 0;
static uint32_t mic_log_sample_count = 0;
static uint32_t mic_log_adc_errors = 0;
static bool mic_send_seen = false;
static int mic_log_raw_min = 0;
static int mic_log_raw_max = 0;
static int32_t mic_log_pcm_peak = 0;
static uint64_t mic_log_raw_sum = 0;
static uint64_t mic_log_pcm_abs_sum = 0;

static int16_t audio_adc_raw_to_pcm(int raw)
{
    int32_t pcm = (raw - MIC_ADC_CENTER) << MIC_GAIN_SHIFT;

    if (pcm > INT16_MAX) {
        return INT16_MAX;
    }
    if (pcm < INT16_MIN) {
        return INT16_MIN;
    }

    return (int16_t)pcm;
}

static void audio_log_mic_level(int raw, int16_t pcm, bool adc_ok)
{
    int32_t pcm_abs = pcm;
    if (pcm_abs < 0) {
        pcm_abs = -pcm_abs;
    }

    if (mic_log_sample_count == 0) {
        mic_log_raw_min = raw;
        mic_log_raw_max = raw;
        mic_log_pcm_peak = pcm_abs;
    } else {
        if (raw < mic_log_raw_min) {
            mic_log_raw_min = raw;
        }
        if (raw > mic_log_raw_max) {
            mic_log_raw_max = raw;
        }
        if (pcm_abs > mic_log_pcm_peak) {
            mic_log_pcm_peak = pcm_abs;
        }
    }

    mic_log_sample_count++;
    mic_log_raw_sum += raw;
    mic_log_pcm_abs_sum += pcm_abs;
    if (!adc_ok) {
        mic_log_adc_errors++;
    }

    uint32_t now_ms = esp_log_timestamp();
    if ((now_ms - mic_log_last_ms) < MIC_LEVEL_LOG_INTERVAL_MS) {
        return;
    }

    if (mic_log_sample_count == 0) {
        return;
    }

    ESP_LOGI(AUDIO_TAG,
             "Mic input GPIO34: raw min=%d avg=%llu max=%d, pcm abs_avg=%llu peak=%ld, samples=%lu, adc_errors=%lu",
             mic_log_raw_min,
             mic_log_raw_sum / mic_log_sample_count,
             mic_log_raw_max,
             mic_log_pcm_abs_sum / mic_log_sample_count,
             mic_log_pcm_peak,
             mic_log_sample_count,
             mic_log_adc_errors);

    mic_log_last_ms = now_ms;
    mic_log_sample_count = 0;
    mic_log_adc_errors = 0;
    mic_log_raw_sum = 0;
    mic_log_pcm_abs_sum = 0;
    mic_log_pcm_peak = 0;
}

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

    adc_oneshot_unit_init_cfg_t adc_init_cfg = {
        .unit_id = MIC_ADC_UNIT,
    };

    esp_err_t err = adc_oneshot_new_unit(&adc_init_cfg, &adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return;
    }

    adc_oneshot_chan_cfg_t adc_chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    err = adc_oneshot_config_channel(adc_handle, MIC_ADC_CHANNEL, &adc_chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "adc_oneshot_config_channel GPIO34 failed: %s", esp_err_to_name(err));
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

    err = dac_continuous_new_channels(&dac_cfg, &dac_handle);
    if (err != ESP_OK) {
        ESP_LOGE(AUDIO_TAG, "dac_continuous_new_channels failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_ok = xTaskCreate(audio_output_task, "audio_out", AUDIO_TASK_STACK_BYTES, NULL, 5, &audio_task_handle);
    if (task_ok != pdPASS) {
        ESP_LOGE(AUDIO_TAG, "Failed to create audio task");
        return;
    }

    ESP_LOGI(AUDIO_TAG, "Audio initialised (DAC CH1 / GPIO26, mic ADC1 CH6 / GPIO34, %d Hz)", SAMPLE_RATE);
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
    if (!buf || len == 0) {
        return 0;
    }

    if (!mic_send_seen) {
        mic_send_seen = true;
        ESP_LOGW(AUDIO_TAG, "audio_send is active; reading mic from GPIO34");
    }

    uint32_t sample_count = len / 2;

    for (uint32_t i = 0; i < sample_count; i++) {
        int raw = MIC_ADC_CENTER;
        bool adc_ok = false;
        if (adc_handle) {
            esp_err_t err = adc_oneshot_read(adc_handle, MIC_ADC_CHANNEL, &raw);
            if (err == ESP_OK) {
                adc_ok = true;
            } else {
                raw = MIC_ADC_CENTER;
            }
        }

        int16_t pcm = audio_adc_raw_to_pcm(raw);
        audio_log_mic_level(raw, pcm, adc_ok);
        buf[i * 2] = (uint8_t)(pcm & 0xff);
        buf[i * 2 + 1] = (uint8_t)((pcm >> 8) & 0xff);
    }

    if ((len % 2) != 0) {
        buf[len - 1] = 0;
    }

    return len;
}
