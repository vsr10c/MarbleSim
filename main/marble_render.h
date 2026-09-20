#pragma once

#include "lvgl.h"
#include "marble_physics.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MARBLE_RENDER_DIAMETER 48
#define MARBLE_RENDER_RADIUS   24

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *arena_canvas;
    lv_obj_t *shadow_img;
    lv_obj_t *marble_img;
    lv_obj_t *fps_label;
    lv_obj_t *info_label;
    lv_obj_t *tilt_label;
    lv_obj_t *calib_label;
    bool      hud_visible;
} marble_render_context_t;

/**
 * @brief Initialize all graphical elements for the marble simulator
 * 
 * Must be called with bsp_display_lock held.
 */
bool marble_render_init(marble_render_context_t *ctx, const arena_config_t *arena);

/**
 * @brief Update visual position, rotation, and HUD labels
 * 
 * Must be called with bsp_display_lock held.
 * 
 * @param ctx Render context
 * @param state Physics state
 * @param tilt_x_deg Current tilt angle X in degrees
 * @param tilt_y_deg Current tilt angle Y in degrees
 * @param current_fps Live FPS measurement
 */
void marble_render_update(marble_render_context_t *ctx,
                          const marble_state_t *state,
                          float tilt_x_deg,
                          float tilt_y_deg,
                          float current_fps);

/**
 * @brief Toggle HUD visibility on/off
 */
void marble_render_toggle_hud(marble_render_context_t *ctx);

/**
 * @brief Trigger impact pulse animation at the rim
 */
/**
 * @brief Show or clear calibration feedback banner
 */
void marble_render_show_calib_feedback(marble_render_context_t *ctx, bool active);

#ifdef __cplusplus
}
#endif

