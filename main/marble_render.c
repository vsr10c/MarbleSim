#include "marble_render.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "marble_render";

// Statically allocated pixel buffers for procedural sprites
static uint8_t s_marble_pixel_data[MARBLE_RENDER_DIAMETER * MARBLE_RENDER_DIAMETER * 4];
static uint8_t s_shadow_pixel_data[MARBLE_RENDER_DIAMETER * MARBLE_RENDER_DIAMETER * 4];

static lv_image_dsc_t s_marble_dsc = {
    .header = {
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = MARBLE_RENDER_DIAMETER,
        .h = MARBLE_RENDER_DIAMETER,
        .stride = MARBLE_RENDER_DIAMETER * 4,
    },
    .data_size = sizeof(s_marble_pixel_data),
    .data = s_marble_pixel_data,
};

static lv_image_dsc_t s_shadow_dsc = {
    .header = {
        .cf = LV_COLOR_FORMAT_ARGB8888,
        .w = MARBLE_RENDER_DIAMETER,
        .h = MARBLE_RENDER_DIAMETER,
        .stride = MARBLE_RENDER_DIAMETER * 4,
    },
    .data_size = sizeof(s_shadow_pixel_data),
    .data = s_shadow_pixel_data,
};

/**
 * @brief Procedurally generate photorealistic AISI 316 stainless steel marble texture
 */
static void bake_stainless_steel_texture(void) {
    const float radius = (float)MARBLE_RENDER_RADIUS;
    const float center = (float)MARBLE_RENDER_RADIUS - 0.5f;

    // Light source vector: from upper-left and slightly forward (normalized)
    const float lx = -0.55f;
    const float ly = -0.65f;
    const float lz = 0.52f;
    const float l_len = sqrtf(lx*lx + ly*ly + lz*lz);
    const float nlx = lx / l_len;
    const float nly = ly / l_len;
    const float nlz = lz / l_len;

    // View direction (looking down -Z onto XY plane)
    const float nvx = 0.0f;
    const float nvy = 0.0f;
    const float nvz = 1.0f;

    // Halfway vector for Blinn-Phong specular
    const float hx = nlx + nvx;
    const float hy = nly + nvy;
    const float hz = nlz + nvz;
    const float h_len = sqrtf(hx*hx + hy*hy + hz*hz);
    const float nhx = hx / h_len;
    const float nhy = hy / h_len;
    const float nhz = hz / h_len;

    for (int y = 0; y < MARBLE_RENDER_DIAMETER; y++) {
        for (int x = 0; x < MARBLE_RENDER_DIAMETER; x++) {
            int idx = (y * MARBLE_RENDER_DIAMETER + x) * 4;

            float px = (float)x - center;
            float py = (float)y - center;
            float dist = sqrtf(px * px + py * py);
            float norm_dist = dist / radius;

            if (norm_dist >= 1.02f) {
                // Fully transparent outside sphere
                s_marble_pixel_data[idx + 0] = 0; // B
                s_marble_pixel_data[idx + 1] = 0; // G
                s_marble_pixel_data[idx + 2] = 0; // R
                s_marble_pixel_data[idx + 3] = 0; // A
                continue;
            }

            // Anti-aliased boundary alpha
            float alpha = 1.0f;
            if (norm_dist > 0.94f) {
                alpha = (1.02f - norm_dist) / (1.02f - 0.94f);
                if (alpha < 0.0f) alpha = 0.0f;
                if (alpha > 1.0f) alpha = 1.0f;
            }

            // Surface normal on unit sphere
            float nx = px / radius;
            float ny = py / radius;
            float nz = sqrtf(fmaxf(0.0f, 1.0f - nx*nx - ny*ny));

            // 1. Diffuse reflection (N dot L)
            float n_dot_l = fmaxf(0.0f, nx * nlx + ny * nly + nz * nlz);

            // 2. Specular reflection (Blinn-Phong) (N dot H)^shininess
            float n_dot_h = fmaxf(0.0f, nx * nhx + ny * nhy + nz * nhz);
            float specular = powf(n_dot_h, 48.0f); // Sharp chrome specular highlight

            // 3. Fresnel rim reflection (Schlick's approximation for stainless steel)
            float n_dot_v = fmaxf(0.0f, nz); // nv = (0,0,1)
            float f0 = 0.65f; // Stainless steel high reflectance base
            float fresnel = f0 + (1.0f - f0) * powf(1.0f - n_dot_v, 3.5f);

            // 4. Subtle brushed steel anisotropic grain
            float angle = atan2f(py, px);
            float grain = 0.96f + 0.04f * sinf(angle * 8.0f);

            // 5. Environmental chrome reflection gradient (sky reflection above, ground below)
            float env_y = (ny + 1.0f) * 0.5f; // 0 to 1
            float base_r = 135.0f + 50.0f * (1.0f - env_y) + 30.0f * fresnel;
            float base_g = 145.0f + 55.0f * (1.0f - env_y) + 35.0f * fresnel;
            float base_b = 160.0f + 65.0f * (1.0f - env_y) + 40.0f * fresnel;

            // Combine lighting layers
            float final_r = (base_r * (0.35f + 0.65f * n_dot_l) + specular * 255.0f) * grain;
            float final_g = (base_g * (0.35f + 0.65f * n_dot_l) + specular * 255.0f) * grain;
            float final_b = (base_b * (0.35f + 0.65f * n_dot_l) + specular * 255.0f) * grain;

            // Clamp colors
            if (final_r > 255.0f) final_r = 255.0f;
            if (final_g > 255.0f) final_g = 255.0f;
            if (final_b > 255.0f) final_b = 255.0f;

            s_marble_pixel_data[idx + 0] = (uint8_t)final_b;               // Blue
            s_marble_pixel_data[idx + 1] = (uint8_t)final_g;               // Green
            s_marble_pixel_data[idx + 2] = (uint8_t)final_r;               // Red
            s_marble_pixel_data[idx + 3] = (uint8_t)(alpha * 255.0f + 0.5f); // Alpha
        }
    }
}

/**
 * @brief Procedurally generate soft drop shadow texture
 */
static void bake_drop_shadow_texture(void) {
    const float radius = (float)MARBLE_RENDER_RADIUS;
    const float center = (float)MARBLE_RENDER_RADIUS - 0.5f;

    for (int y = 0; y < MARBLE_RENDER_DIAMETER; y++) {
        for (int x = 0; x < MARBLE_RENDER_DIAMETER; x++) {
            int idx = (y * MARBLE_RENDER_DIAMETER + x) * 4;

            float px = (float)x - center;
            float py = (float)y - center;
            float dist = sqrtf(px * px + py * py);
            float norm_dist = dist / radius;

            if (norm_dist >= 1.0f) {
                s_shadow_pixel_data[idx + 0] = 0;
                s_shadow_pixel_data[idx + 1] = 0;
                s_shadow_pixel_data[idx + 2] = 0;
                s_shadow_pixel_data[idx + 3] = 0;
                continue;
            }

            // Smooth quadratic falloff from center (peak shadow alpha ~130)
            float falloff = 1.0f - (norm_dist * norm_dist);
            uint8_t shadow_alpha = (uint8_t)(falloff * 135.0f);

            s_shadow_pixel_data[idx + 0] = 0; // Pure black shadow
            s_shadow_pixel_data[idx + 1] = 0;
            s_shadow_pixel_data[idx + 2] = 0;
            s_shadow_pixel_data[idx + 3] = shadow_alpha;
        }
    }
}

/**
 * @brief Helper to create static circular ring guide lines
 */
static lv_obj_t *create_guide_ring(lv_obj_t *parent, int diameter, lv_color_t color, lv_opa_t opa, int border_width) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, diameter, diameter);
    lv_obj_center(obj);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, border_width, 0);
    lv_obj_set_style_border_color(obj, color, 0);
    lv_obj_set_style_border_opa(obj, opa, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

bool marble_render_init(marble_render_context_t *ctx, const arena_config_t *arena) {
    if (!ctx || !arena) return false;

    ESP_LOGI(TAG, "Baking stainless steel textures...");
    bake_stainless_steel_texture();
    bake_drop_shadow_texture();

    ctx->screen = lv_screen_active();
    lv_obj_set_style_bg_color(ctx->screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ctx->screen, LV_OBJ_FLAG_SCROLLABLE);

    // 1. Create AMOLED Circular Arena Guide Rings
    // Outer metallic boundary rim (Radius = 228 px -> Diameter = 456 px)
    create_guide_ring(ctx->screen, (int)(arena->arena_radius * 2.0f), lv_color_hex(0x4A5568), LV_OPA_80, 2);

    // Mid-range graduations (150px and 75px radius)
    create_guide_ring(ctx->screen, 300, lv_color_hex(0x2D3748), LV_OPA_50, 1);
    create_guide_ring(ctx->screen, 150, lv_color_hex(0x2D3748), LV_OPA_50, 1);

    // Center spirit-level bullseye target (Radius = 25px -> Diameter = 50px)
    create_guide_ring(ctx->screen, 50, lv_color_hex(0x4A5568), LV_OPA_70, 1);

    // Center dot (4x4 px)
    lv_obj_t *center_dot = lv_obj_create(ctx->screen);
    lv_obj_set_size(center_dot, 6, 6);
    lv_obj_center(center_dot);
    lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center_dot, lv_color_hex(0x718096), 0);
    lv_obj_set_style_bg_opa(center_dot, LV_OPA_80, 0);
    lv_obj_set_style_border_width(center_dot, 0, 0);
    lv_obj_remove_flag(center_dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // 2. Drop Shadow Image Object (rendered below the marble)
    ctx->shadow_img = lv_image_create(ctx->screen);
    lv_image_set_src(ctx->shadow_img, &s_shadow_dsc);
    lv_obj_remove_flag(ctx->shadow_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ctx->shadow_img, (int)arena->center_x - MARBLE_RENDER_RADIUS + 4,
                                     (int)arena->center_y - MARBLE_RENDER_RADIUS + 6);

    // 3. Marble Image Object
    ctx->marble_img = lv_image_create(ctx->screen);
    lv_image_set_src(ctx->marble_img, &s_marble_dsc);
    lv_image_set_pivot(ctx->marble_img, MARBLE_RENDER_RADIUS, MARBLE_RENDER_RADIUS);
    lv_obj_remove_flag(ctx->marble_img, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(ctx->marble_img, (int)arena->center_x - MARBLE_RENDER_RADIUS,
                                    (int)arena->center_y - MARBLE_RENDER_RADIUS);

    // 4. HUD Labels
    ctx->hud_visible = true;

    // FPS & Latency (Top Center)
    ctx->fps_label = lv_label_create(ctx->screen);
    lv_obj_set_style_text_color(ctx->fps_label, lv_color_hex(0x63B3ED), 0); // Cyan-blue
    lv_obj_set_style_text_font(ctx->fps_label, &lv_font_montserrat_14, 0);
    lv_label_set_text(ctx->fps_label, "60 FPS");
    lv_obj_align(ctx->fps_label, LV_ALIGN_TOP_MID, 0, 18);

    // Material Specification (Sub-top)
    ctx->info_label = lv_label_create(ctx->screen);
    lv_obj_set_style_text_color(ctx->info_label, lv_color_hex(0xA0AEC0), 0);
    lv_obj_set_style_text_font(ctx->info_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(ctx->info_label, "AISI 316 STEEL • e=0.85");
    lv_obj_align(ctx->info_label, LV_ALIGN_TOP_MID, 0, 36);

    // Tilt & Velocity (Bottom Mid)
    ctx->tilt_label = lv_label_create(ctx->screen);
    lv_obj_set_style_text_color(ctx->tilt_label, lv_color_hex(0xE2E8F0), 0);
    lv_obj_set_style_text_font(ctx->tilt_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(ctx->tilt_label, "Tilt: 0.0° | Vel: 0 mm/s");
    lv_obj_align(ctx->tilt_label, LV_ALIGN_BOTTOM_MID, 0, -38);

    // Calibration Hint (Bottom Edge)
    ctx->calib_label = lv_label_create(ctx->screen);
    lv_obj_set_style_text_color(ctx->calib_label, lv_color_hex(0x718096), 0);
    lv_obj_set_style_text_font(ctx->calib_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(ctx->calib_label, "Press BOOT to Level");
    lv_obj_align(ctx->calib_label, LV_ALIGN_BOTTOM_MID, 0, -20);

    ESP_LOGI(TAG, "Marble render initialized successfully");
    return true;
}

void marble_render_update(marble_render_context_t *ctx,
                          const marble_state_t *state,
                          float tilt_x_deg,
                          float tilt_y_deg,
                          float current_fps) {
    if (!ctx || !state) return;

    // 1. Update position of marble and drop shadow
    int marble_x = (int)(state->pos_x - (float)MARBLE_RENDER_RADIUS + 0.5f);
    int marble_y = (int)(state->pos_y - (float)MARBLE_RENDER_RADIUS + 0.5f);

    lv_obj_set_pos(ctx->shadow_img, marble_x + 4, marble_y + 5);
    lv_obj_set_pos(ctx->marble_img, marble_x, marble_y);

    // 2. Update visual rotation angle matching physical rolling displacement
    // In LVGL v9, angle is in 0.1 degree units (0 to 3600)
    int angle_0_1_deg = (int)(state->roll_angle_rad * 572.957795f) % 3600;
    if (angle_0_1_deg < 0) angle_0_1_deg += 3600;
    lv_image_set_rotation(ctx->marble_img, angle_0_1_deg);

    // 3. Update HUD labels (throttle label text updates if needed)
    if (ctx->hud_visible) {
        char buf[64];

        // FPS
        snprintf(buf, sizeof(buf), "%.0f FPS", current_fps);
        lv_label_set_text(ctx->fps_label, buf);

        // Tilt & Speed (show explicit X and Y degrees for intuitive alignment)
        snprintf(buf, sizeof(buf), "Tilt: X:%+.1f° Y:%+.1f° | %.0f mm/s", tilt_x_deg, tilt_y_deg, state->speed_mm_s);
        lv_label_set_text(ctx->tilt_label, buf);
    }
}

void marble_render_show_calib_feedback(marble_render_context_t *ctx, bool active) {
    if (!ctx || !ctx->calib_label) return;
    if (active) {
        lv_obj_set_style_text_color(ctx->calib_label, lv_color_hex(0x48BB78), 0); // Vibrant Green
        lv_label_set_text(ctx->calib_label, "✓ ZEROED & CENTERED");
    } else {
        lv_obj_set_style_text_color(ctx->calib_label, lv_color_hex(0x718096), 0); // Neutral Gray
        lv_label_set_text(ctx->calib_label, "Press BOOT or Tap Center to Level");
    }
}

void marble_render_toggle_hud(marble_render_context_t *ctx) {
    if (!ctx) return;
    ctx->hud_visible = !ctx->hud_visible;

    if (ctx->hud_visible) {
        lv_obj_remove_flag(ctx->fps_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->info_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->tilt_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ctx->calib_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->fps_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->info_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->tilt_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->calib_label, LV_OBJ_FLAG_HIDDEN);
    }
}

void marble_render_trigger_impact_flash(marble_render_context_t *ctx, float intensity) {
    if (!ctx) return;
}

