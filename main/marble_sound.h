#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ES8311 speaker codec and Core 0 audio task
 * 
 * Configures the onboard ES8311 I2S codec via BSP, pre-synthesizes the AISI 316
 * metallic strike waveform, and launches a background FreeRTOS audio task pinned
 * to CPU Core 0.
 * 
 * @return ESP_OK on success, or error code
 */
esp_err_t marble_sound_init(void);

/**
 * @brief Enable or disable audio output
 * 
 * @param enabled True to enable sounds, false to mute
 */
void marble_sound_set_enabled(bool enabled);

/**
 * @brief Check if audio output is currently enabled
 * 
 * @return True if audio is enabled, false if muted
 */
bool marble_sound_is_enabled(void);

/**
 * @brief Toggle audio enabled state
 * 
 * @return New state (true if enabled, false if muted)
 */
bool marble_sound_toggle(void);

/**
 * @brief Trigger metallic impact sound based on collision normal velocity
 * 
 * This function is non-blocking and executes in < 2 microseconds on Core 1 by
 * posting to an event queue processed asynchronously by Core 0.
 * 
 * @param normal_impact_speed Impact velocity normal to the circular rim (px/s)
 */
void marble_sound_trigger_impact(float normal_impact_speed);

#ifdef __cplusplus
}
#endif
