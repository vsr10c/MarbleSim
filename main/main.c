#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

/* qmi8658.h defines M_PI; IDF v6 picolibc already provides it. */
#ifdef M_PI
#undef M_PI
#endif
#include "qmi8658.h"

#include "marble_physics.h"
#include "marble_render.h"
#include "marble_sound.h"
#include "marble_pmic.h"

static const char *TAG = "marble_main";

#define TARGET_FPS              60
#define TARGET_FRAME_TIME_US    (1000000 / TARGET_FPS) // ~16666 us
#define DISPLAY_LOCK_TIMEOUT_MS 50
#define SENSOR_DEADZONE_MSS     0.05f
#define CALIB_BUTTON_GPIO       GPIO_NUM_0

// Global simulation objects
static marble_properties_t s_marble_props;
static marble_state_t      s_marble_state;
static arena_config_t      s_arena_cfg;
static marble_render_context_t s_render_ctx;

// Sensor calibration biases
static float s_accel_bias_x = 0.0f;
static float s_accel_bias_y = 0.0f;
static volatile bool s_recalibration_requested = false;
static int s_toast_timer = 0;

// Interactive touch grab & fling state
static volatile bool s_touch_grabbed = false;
static float s_drag_prev_x = 0.0f;
static float s_drag_prev_y = 0.0f;
static int64_t s_drag_prev_time_us = 0;
static float s_fling_vx = 0.0f;
static float s_fling_vy = 0.0f;

// Adaptive 1€ (One Euro) Filter for zero-latency jitter-free tilt response
typedef struct {
    float x_prev;
    float dx_prev;
    float fc_min;   // Minimum cutoff frequency (Hz) at rest
    float beta;     // Speed adaptation coefficient
    float d_cutoff; // Derivative cutoff frequency (Hz)
    bool  initialized;
} one_euro_filter_t;

static one_euro_filter_t s_filter_x;
static one_euro_filter_t s_filter_y;

static void one_euro_init(one_euro_filter_t *f, float fc_min, float beta, float d_cutoff) {
    f->x_prev = 0.0f;
    f->dx_prev = 0.0f;
    f->fc_min = fc_min;
    f->beta = beta;
    f->d_cutoff = d_cutoff;
    f->initialized = false;
}

static float one_euro_step(one_euro_filter_t *f, float x, float dt) {
    if (dt <= 0.0f) return x;
    if (!f->initialized) {
        f->x_prev = x;
        f->dx_prev = 0.0f;
        f->initialized = true;
        return x;
    }

    // 1. Filter derivative of signal to estimate speed of change
    float dx = (x - f->x_prev) / dt;
    float alpha_d = 1.0f / (1.0f + 1.0f / (2.0f * (float)M_PI * f->d_cutoff * dt));
    if (alpha_d > 1.0f) alpha_d = 1.0f;
    float dx_hat = alpha_d * dx + (1.0f - alpha_d) * f->dx_prev;
    f->dx_prev = dx_hat;

    // 2. Compute dynamic cutoff frequency: expands during fast movement, drops at rest
    float fc = f->fc_min + f->beta * fabsf(dx_hat);
    float alpha = 1.0f / (1.0f + 1.0f / (2.0f * (float)M_PI * fc * dt));
    if (alpha > 0.95f) alpha = 0.95f;
    if (alpha < 0.03f) alpha = 0.03f;

    // 3. Filter position/acceleration
    float x_hat = alpha * x + (1.0f - alpha) * f->x_prev;
    f->x_prev = x_hat;
    return x_hat;
}

static void init_calibration_button(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CALIB_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE, // Polled reliably in 60 Hz task loop
    };
    gpio_config(&io_conf);
}

/**
 * @brief Zero level calibration and snap marble to center
 */
static void perform_calibration_and_center(qmi8658_dev_t *dev) {
    ESP_LOGI(TAG, "Recalibrating level and centering marble...");
    qmi8658_data_t data;
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    int count = 0;

    // Fast 25-sample average (~50 ms)
    for (int i = 0; i < 25; i++) {
        if (qmi8658_read_sensor_data(dev, &data) == ESP_OK) {
            sum_x += data.accelX;
            sum_y += data.accelY;
            count++;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    if (count > 0) {
        s_accel_bias_x = sum_x / (float)count;
        s_accel_bias_y = sum_y / (float)count;
        ESP_LOGI(TAG, "Calibration OK! Bias X: %.4f m/s², Bias Y: %.4f m/s²",
                 s_accel_bias_x, s_accel_bias_y);
    }

    // 1. Instantly reset physical state to the exact center of the circular arena
    marble_physics_init_state(&s_marble_state, s_arena_cfg.center_x, s_arena_cfg.center_y);
    s_marble_state.vel_x = 0.0f;
    s_marble_state.vel_y = 0.0f;
    s_marble_state.accel_x = 0.0f;
    s_marble_state.accel_y = 0.0f;
    s_marble_state.speed_px_s = 0.0f;
    s_marble_state.speed_mm_s = 0.0f;

    // Reset filters
    s_filter_x.x_prev = 0.0f;
    s_filter_x.dx_prev = 0.0f;
    s_filter_y.x_prev = 0.0f;
    s_filter_y.dx_prev = 0.0f;

    // 2. Trigger on-screen visual confirmation banner for 2 seconds (120 frames @ 60 FPS)
    s_toast_timer = 120;
    if (bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS) == ESP_OK) {
        marble_render_show_calib_feedback(&s_render_ctx, true);
        marble_render_update(&s_render_ctx, &s_marble_state, 0.0f, 0.0f, 60.0f);
        bsp_display_unlock();
    }
}

static void screen_touch_event_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t pt;
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_indev_get_point(indev, &pt);

    if (code == LV_EVENT_PRESSED) {
        float dx = (float)pt.x - s_marble_state.pos_x;
        float dy = (float)pt.y - s_marble_state.pos_y;
        float dist = sqrtf(dx * dx + dy * dy);

        // Direct grab if pressed within marble perimeter (+ 14px touch tolerance)
        if (dist <= 38.0f) {
            s_touch_grabbed = true;
            s_drag_prev_x = (float)pt.x;
            s_drag_prev_y = (float)pt.y;
            s_drag_prev_time_us = esp_timer_get_time();
            s_fling_vx = 0.0f;
            s_fling_vy = 0.0f;
            s_marble_state.vel_x = 0.0f;
            s_marble_state.vel_y = 0.0f;
            return;
        }
    } else if (code == LV_EVENT_PRESSING) {
        if (s_touch_grabbed) {
            // Drag marble directly under finger, clamped strictly to arena boundary
            float cdx = (float)pt.x - s_arena_cfg.center_x;
            float cdy = (float)pt.y - s_arena_cfg.center_y;
            float cdist = sqrtf(cdx * cdx + cdy * cdy);
            float max_r = s_arena_cfg.arena_radius - s_marble_props.radius_px;

            float new_x = (float)pt.x;
            float new_y = (float)pt.y;
            if (cdist > max_r) {
                new_x = s_arena_cfg.center_x + (cdx / cdist) * max_r;
                new_y = s_arena_cfg.center_y + (cdy / cdist) * max_r;
            }

            s_marble_state.pos_x = new_x;
            s_marble_state.pos_y = new_y;

            // Track instantaneous finger velocity for physical fling release
            int64_t now_us = esp_timer_get_time();
            float dt = (float)(now_us - s_drag_prev_time_us) / 1000000.0f;
            if (dt > 0.005f) {
                float inst_vx = (new_x - s_drag_prev_x) / dt;
                float inst_vy = (new_y - s_drag_prev_y) / dt;
                s_fling_vx = 0.65f * inst_vx + 0.35f * s_fling_vx;
                s_fling_vy = 0.65f * inst_vy + 0.35f * s_fling_vy;
                s_drag_prev_x = new_x;
                s_drag_prev_y = new_y;
                s_drag_prev_time_us = now_us;
            }
            return;
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (s_touch_grabbed) {
            s_touch_grabbed = false;
            // Transfer finger fling velocity directly into physical simulation
            float fling_speed = sqrtf(s_fling_vx * s_fling_vx + s_fling_vy * s_fling_vy);
            if (fling_speed > 1800.0f) {
                s_fling_vx = (s_fling_vx / fling_speed) * 1800.0f;
                s_fling_vy = (s_fling_vy / fling_speed) * 1800.0f;
            }
            s_marble_state.vel_x = s_fling_vx;
            s_marble_state.vel_y = s_fling_vy;
            return;
        }
    } else if (code == LV_EVENT_CLICKED) {
        // Distance from screen center
        float dist_from_center = sqrtf(((float)pt.x - s_arena_cfg.center_x) * ((float)pt.x - s_arena_cfg.center_x) +
                                       ((float)pt.y - s_arena_cfg.center_y) * ((float)pt.y - s_arena_cfg.center_y));

        // 1. Tapping near center bullseye or bottom calibration label triggers calibration & center
        if (dist_from_center < 35.0f || pt.y > 410) {
            s_recalibration_requested = true;
            return;
        }

        // 2. Tapping near top edge toggles HUD
        if (pt.y < 55) {
            marble_render_toggle_hud(&s_render_ctx);
            return;
        }

        // 3. Tapping elsewhere applies flick impulse toward the tap point
        float dx = (float)pt.x - s_marble_state.pos_x;
        float dy = (float)pt.y - s_marble_state.pos_y;
        float dist_from_marble = sqrtf(dx * dx + dy * dy);

        if (dist_from_marble > 5.0f) {
            float impulse_speed = 380.0f; // px/s
            marble_physics_apply_impulse(&s_marble_state, (dx / dist_from_marble) * impulse_speed,
                                                          (dy / dist_from_marble) * impulse_speed);
        }
    }
}

static void marble_sim_task(void *arg) {
    qmi8658_dev_t *dev = (qmi8658_dev_t *)arg;
    qmi8658_data_t data;

    // Button debounce tracker
    int s_btn_prev = 1;

    // Timing trackers
    int64_t last_time_us = esp_timer_get_time();
    int64_t fps_timer_us = last_time_us;
    int frame_count = 0;
    float current_fps = 60.0f;

    ESP_LOGI(TAG, "Marble simulation task started @ target 60 FPS");

    while (1) {
        int64_t frame_start_us = esp_timer_get_time();

        // 1. Check BOOT button (GPIO 0) polled directly in 60 Hz task
        int btn_curr = gpio_get_level(CALIB_BUTTON_GPIO);
        if (s_btn_prev == 1 && btn_curr == 0) {
            // Button pressed (falling edge on active-LOW BOOT button)
            s_recalibration_requested = true;
        }
        s_btn_prev = btn_curr;

        // 2. Check Power Button (AXP2101 PEK key) every 8 frames (~133 ms)
        if ((frame_count % 8) == 0) {
            if (marble_pmic_poll_power_key()) {
                bool enabled = marble_sound_toggle();
                s_toast_timer = 120; // 2 seconds @ 60 FPS
                if (bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS) == ESP_OK) {
                    marble_render_show_toast(&s_render_ctx,
                                             enabled ? "AUDIO: ON 🔊" : "AUDIO: OFF 🔇",
                                             true);
                    bsp_display_unlock();
                }
            }
        }

        // 3. Handle calibration & center request
        if (s_recalibration_requested) {
            s_recalibration_requested = false;
            perform_calibration_and_center(dev);
            last_time_us = esp_timer_get_time();
        }

        // 4. Status toast timer countdown
        if (s_toast_timer > 0) {
            s_toast_timer--;
            if (s_toast_timer == 0) {
                if (bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS) == ESP_OK) {
                    marble_render_show_toast(&s_render_ctx, NULL, false);
                    bsp_display_unlock();
                }
            }
        }

        // 5. Calculate delta time
        float dt = (float)(frame_start_us - last_time_us) / 1000000.0f;
        last_time_us = frame_start_us;

        // Clamp dt to avoid physics blow-up if task was delayed
        if (dt > 0.05f) dt = 0.05f;
        if (dt <= 0.0f) dt = 0.01666f;

        // 6. Read IMU accelerometer & gyroscope
        float raw_ax = 0.0f;
        float raw_ay = 0.0f;
        if (qmi8658_read_sensor_data(dev, &data) == ESP_OK) {
            // Subtract calibrated zero-level bias
            float calib_x = data.accelX - s_accel_bias_x;
            float calib_y = data.accelY - s_accel_bias_y;

            // Apply deadzone threshold to eliminate sensor resting noise
            if (fabsf(calib_x) < SENSOR_DEADZONE_MSS) calib_x = 0.0f;
            if (fabsf(calib_y) < SENSOR_DEADZONE_MSS) calib_y = 0.0f;

            // Real gravity coordinate mapping for Waveshare ESP32-S3-Touch-AMOLED-1.75C:
            // - Tilting board right (towards crown/button) -> +X -> +calib_y
            // - Tilting board down (towards 6 o'clock)      -> +Y -> +calib_x
            raw_ax = calib_y;
            raw_ay = calib_x;

            // Gyroscope angular rate feedforward:
            // Instantly injects wrist angular velocity into acceleration before proof mass settles
            const float k_gyro = 0.12f;
            raw_ax += data.gyroY * k_gyro;
            raw_ay += data.gyroX * k_gyro;
        }

        // 7. Adaptive 1€ Filter: zero jitter at rest, instantaneous reaction during hand movement
        float filt_ax = one_euro_step(&s_filter_x, raw_ax, dt);
        float filt_ay = one_euro_step(&s_filter_y, raw_ay, dt);

        // 8. Calculate physical tilt angles in degrees
        float tilt_x_deg = asinf(fmaxf(-1.0f, fminf(1.0f, filt_ax / 9.80665f))) * 57.29578f;
        float tilt_y_deg = asinf(fmaxf(-1.0f, fminf(1.0f, filt_ay / 9.80665f))) * 57.29578f;

        // 9. Advance physical simulation (only when not directly grabbed by touch)
        if (!s_touch_grabbed) {
            marble_physics_update(&s_marble_state,
                                  &s_marble_props,
                                  &s_arena_cfg,
                                  filt_ax,
                                  filt_ay,
                                  dt);

            // Trigger metallic impact sound on Core 0 whenever circular rim collision occurs
            if (s_marble_state.wall_collided && s_marble_state.last_impact_speed > 30.0f) {
                marble_sound_trigger_impact(s_marble_state.last_impact_speed);
            }
        }

        // 10. Frame rate calculation
        frame_count++;
        int64_t elapsed_fps_us = frame_start_us - fps_timer_us;
        if (elapsed_fps_us >= 500000) { // Update FPS display twice per second
            current_fps = ((float)frame_count * 1000000.0f) / (float)elapsed_fps_us;
            frame_count = 0;
            fps_timer_us = frame_start_us;
        }

        // 11. Render update under thread-safe LVGL display lock
        if (bsp_display_lock(DISPLAY_LOCK_TIMEOUT_MS) == ESP_OK) {
            marble_render_update(&s_render_ctx,
                                 &s_marble_state,
                                 tilt_x_deg,
                                 tilt_y_deg,
                                 current_fps);
            bsp_display_unlock();
        }

        // 12. Precise sleep pacing for rock-solid 60 FPS
        int64_t frame_end_us = esp_timer_get_time();
        int64_t compute_duration_us = frame_end_us - frame_start_us;
        int64_t sleep_us = TARGET_FRAME_TIME_US - compute_duration_us;

        if (sleep_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS(sleep_us / 1000));
        } else {
            taskYIELD();
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting Waveshare ESP32-S3 AMOLED 1.75C Marble Simulator");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize BSP Display & LVGL
    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "Failed to initialize display via BSP");
        return;
    }
    bsp_display_backlight_on();

    // 3. Initialize Marble Physics State & Arena Geometry
    marble_physics_get_default_properties(&s_marble_props);
    marble_physics_get_default_arena(&s_arena_cfg);
    marble_physics_init_state(&s_marble_state, s_arena_cfg.center_x, s_arena_cfg.center_y);

    // Initialize 1€ filters
    one_euro_init(&s_filter_x, 1.0f, 0.05f, 1.2f);
    one_euro_init(&s_filter_y, 1.0f, 0.05f, 1.2f);

    // 4. Initialize Graphics & UI under display lock
    if (bsp_display_lock(500) == ESP_OK) {
        marble_render_init(&s_render_ctx, &s_arena_cfg);
        // Register touch listeners on active screen
        lv_obj_add_event_cb(lv_screen_active(), screen_touch_event_cb, LV_EVENT_ALL, NULL);
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Failed to obtain display lock for render initialization");
    }

    // 5. Initialize Sound Engine (ES8311 Codec on Core 0)
    ret = marble_sound_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Audio initialization failed: %s (Continuing without audio)", esp_err_to_name(ret));
    }

    // 6. Initialize PMIC Power Key Detection (AXP2101 at 0x34)
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_get_handle();
    ret = marble_pmic_init(i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PMIC PEK key detection not available: %s", esp_err_to_name(ret));
    }

    // 7. Initialize QMI8658 IMU on shared I2C bus
    qmi8658_dev_t *imu_dev = malloc(sizeof(qmi8658_dev_t));
    if (!imu_dev) {
        ESP_LOGE(TAG, "Failed to allocate memory for IMU device");
        return;
    }

    ret = qmi8658_init(imu_dev, i2c_bus, QMI8658_ADDRESS_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found at ADDRESS_HIGH (0x6B), trying ADDRESS_LOW (0x6A)...");
        ret = qmi8658_init(imu_dev, i2c_bus, QMI8658_ADDRESS_LOW);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize QMI8658 IMU: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "QMI8658 IMU initialized successfully");
        qmi8658_set_accel_range(imu_dev, QMI8658_ACCEL_RANGE_8G);
        qmi8658_set_accel_odr(imu_dev, QMI8658_ACCEL_ODR_500HZ);
        qmi8658_set_accel_unit_mps2(imu_dev, true);
        qmi8658_set_gyro_range(imu_dev, QMI8658_GYRO_RANGE_512DPS);
        qmi8658_set_gyro_odr(imu_dev, QMI8658_GYRO_ODR_500HZ);
        qmi8658_set_gyro_unit_rads(imu_dev, true);
    }

    // 8. Set up BOOT button (GPIO 0) for zero calibration
    init_calibration_button();

    // 9. Initial zero-level calibration and marble centering
    if (ret == ESP_OK) {
        perform_calibration_and_center(imu_dev);
    }

    // 10. Launch high-frequency simulation task pinned to CPU Core 1
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        marble_sim_task,
        "marble_sim",
        8192,
        imu_dev,
        5,      // Priority
        NULL,
        1       // Pinned to Core 1
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create marble simulation task");
    }
}
