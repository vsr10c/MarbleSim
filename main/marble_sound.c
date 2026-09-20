#include "marble_sound.h"
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

static const char *TAG = "marble_sound";

#define AUDIO_SAMPLE_RATE     22050
#define IMPACT_SAMPLE_COUNT   1024    // ~46.4 ms duration
#define IMPACT_COOLDOWN_US    40000   // 40 ms cooldown between consecutive impacts
#define MIN_AUDIBLE_SPEED_PX  35.0f   // Minimum normal velocity to trigger audio

static esp_codec_dev_handle_t s_spk_dev = NULL;
static QueueHandle_t s_impact_queue = NULL;
static TaskHandle_t s_sound_task_handle = NULL;
static volatile bool s_audio_enabled = true;
static int64_t s_last_impact_time_us = 0;

// Pre-computed AISI 316 metallic clink base waveform
static int16_t s_base_pcm[IMPACT_SAMPLE_COUNT];

static void synthesize_metallic_waveform(void) {
    const float dt = 1.0f / (float)AUDIO_SAMPLE_RATE;
    const float tau = 0.010f; // 10 ms exponential decay envelope

    for (int i = 0; i < IMPACT_SAMPLE_COUNT; i++) {
        float t = (float)i * dt;
        float decay = expf(-t / tau);

        // Acoustic modes of solid steel bearing on metal enclosure rim:
        // f1 = 2400 Hz (fundamental chime)
        // f2 = 4800 Hz (first harmonic ring)
        // f3 = 7200 Hz (high frequency metallic clack)
        float s1 = sinf(2.0f * (float)M_PI * 2400.0f * t);
        float s2 = sinf(2.0f * (float)M_PI * 4800.0f * t);
        float s3 = sinf(2.0f * (float)M_PI * 7200.0f * t);

        float sample = decay * (0.55f * s1 + 0.30f * s2 + 0.15f * s3);

        // Scale to 16-bit PCM range (peak ~22000 to prevent clipping/distortion)
        s_base_pcm[i] = (int16_t)(sample * 22000.0f);
    }
}

static void marble_sound_task(void *arg) {
    float impact_speed = 0.0f;
    int16_t play_buffer[IMPACT_SAMPLE_COUNT];

    ESP_LOGI(TAG, "Audio task active on Core %d", xPortGetCoreID());

    while (1) {
        if (xQueueReceive(s_impact_queue, &impact_speed, portMAX_DELAY) == pdTRUE) {
            if (!s_audio_enabled || !s_spk_dev) {
                continue;
            }

            // Map normal impact speed (~35 to ~500 px/s) to gain range [0.25, 1.0]
            float gain = (impact_speed - MIN_AUDIBLE_SPEED_PX) / 400.0f + 0.25f;
            if (gain > 1.0f) gain = 1.0f;
            if (gain < 0.20f) gain = 0.20f;

            // Dynamically scale precomputed waveform by velocity gain
            for (int i = 0; i < IMPACT_SAMPLE_COUNT; i++) {
                play_buffer[i] = (int16_t)((float)s_base_pcm[i] * gain);
            }

            // Write PCM audio data to ES8311 speaker codec
            esp_codec_dev_write(s_spk_dev, play_buffer, sizeof(play_buffer));
        }
    }
}

esp_err_t marble_sound_init(void) {
    // 1. Synthesize metallic sound sample
    synthesize_metallic_waveform();

    // 2. Initialize speaker codec using official Waveshare BSP
    s_spk_dev = bsp_audio_codec_speaker_init();
    if (!s_spk_dev) {
        ESP_LOGE(TAG, "Failed to initialize speaker codec from BSP");
        return ESP_FAIL;
    }

    // Set hardware master volume (0 to 100)
    esp_codec_dev_set_out_vol(s_spk_dev, 80);

    // Open speaker codec with 22050 Hz mono 16-bit format
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = AUDIO_SAMPLE_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    esp_err_t ret = esp_codec_dev_open(s_spk_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open speaker codec: %d", ret);
        return ESP_FAIL;
    }

    // 3. Create impact event queue
    s_impact_queue = xQueueCreate(4, sizeof(float));
    if (!s_impact_queue) {
        ESP_LOGE(TAG, "Failed to create audio impact queue");
        return ESP_ERR_NO_MEM;
    }

    // 4. Spawn background audio task pinned to Core 0 (rendering runs on Core 1)
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        marble_sound_task,
        "marble_sound",
        4096,
        NULL,
        4,      // Priority
        &s_sound_task_handle,
        0       // Pinned to CPU Core 0
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio task on Core 0");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Marble metallic audio engine initialized successfully");
    return ESP_OK;
}

void marble_sound_set_enabled(bool enabled) {
    s_audio_enabled = enabled;
    ESP_LOGI(TAG, "Audio output %s", enabled ? "ENABLED" : "MUTED");
}

bool marble_sound_is_enabled(void) {
    return s_audio_enabled;
}

bool marble_sound_toggle(void) {
    s_audio_enabled = !s_audio_enabled;
    ESP_LOGI(TAG, "Audio output toggled -> %s", s_audio_enabled ? "ENABLED" : "MUTED");
    return s_audio_enabled;
}

void marble_sound_trigger_impact(float normal_impact_speed) {
    if (!s_audio_enabled || !s_spk_dev || !s_impact_queue) {
        return;
    }

    if (normal_impact_speed < MIN_AUDIBLE_SPEED_PX) {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if ((now_us - s_last_impact_time_us) < IMPACT_COOLDOWN_US) {
        return; // Guard against rapid-fire stutter during continuous rim rolling
    }
    s_last_impact_time_us = now_us;

    // Non-blocking enqueue (0 wait ticks) - will never delay physics/render loop
    xQueueSend(s_impact_queue, &normal_impact_speed, 0);
}
