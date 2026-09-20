#include "marble_physics.h"
#include <math.h>
#include <string.h>

#define GRAVITY_MSS         9.80665f
#define PHYSICS_SUB_STEPS   4       // 4x sub-stepping -> 240 Hz physics simulation at 60 FPS

void marble_physics_get_default_properties(marble_properties_t *props) {
    if (!props) return;
    props->density_kg_m3      = 8000.0f;    // AISI 316 Stainless Steel
    props->diameter_mm        = 14.0f;      // 14mm ball bearing
    props->radius_px          = 24.0f;      // 48px diameter on 466x466 display
    props->mass_kg            = 0.0115f;    // 11.5 grams
    props->rolling_factor     = 5.0f / 7.0f;// 0.7142857f for solid sphere inertia
    props->rolling_resistance = 0.0035f;    // Low rolling resistance on precision crystal
    props->restitution        = 0.68f;      // Crisp metallic bounce
    props->wall_friction      = 0.12f;      // Preserves orbital tangential rim glide
    props->pixels_per_meter   = 13700.0f;
}

void marble_physics_get_default_arena(arena_config_t *arena) {
    if (!arena) return;
    arena->center_x     = 233.0f;
    arena->center_y     = 233.0f;
    arena->arena_radius = 228.0f; // Leaves 5px bezel margin on 466px screen
}

void marble_physics_init_state(marble_state_t *state, float init_x, float init_y) {
    if (!state) return;
    memset(state, 0, sizeof(marble_state_t));
    state->pos_x = init_x;
    state->pos_y = init_y;
}

void marble_physics_apply_impulse(marble_state_t *state, float impulse_vx, float impulse_vy) {
    if (!state) return;
    state->vel_x += impulse_vx;
    state->vel_y += impulse_vy;
}

void marble_physics_update(marble_state_t *state,
                            const marble_properties_t *props,
                            const arena_config_t *arena,
                            float tilt_accel_x,
                            float tilt_accel_y,
                            float dt_seconds) {
    if (!state || !props || !arena || dt_seconds <= 0.0f) return;

    // Sub-step delta time (4x sub-stepping yields 240 Hz internal physics)
    const float dt_sub = dt_seconds / (float)PHYSICS_SUB_STEPS;
    const float gravity_scale = 1550.0f; // Calibrated px/s^2 per m/s^2

    bool any_wall_collision = false;
    float max_impact_speed = 0.0f;

    for (int step = 0; step < PHYSICS_SUB_STEPS; step++) {
        // 1. Subtle concave bowl restorative curvature
        // Simulates authentic spirit-level curved vial / watch crystal glass
        float rx = state->pos_x - arena->center_x;
        float ry = state->pos_y - arena->center_y;
        float r_dist = sqrtf(rx * rx + ry * ry);

        float dish_ax = 0.0f;
        float dish_ay = 0.0f;
        if (r_dist > 0.5f) {
            // Gentle restoring acceleration toward dial center (up to ~35 px/s^2 at outer edge)
            float k_dish = 35.0f * (r_dist / arena->arena_radius);
            dish_ax = -(rx / r_dist) * k_dish;
            dish_ay = -(ry / r_dist) * k_dish;
        }

        // 2. Gravitational drive acceleration scaled with rotational inertia factor (5/7)
        float drive_ax = (tilt_accel_x * gravity_scale * props->rolling_factor) + dish_ax;
        float drive_ay = (tilt_accel_y * gravity_scale * props->rolling_factor) + dish_ay;

        // 3. Rolling resistance & viscous surface drag
        float current_speed = sqrtf(state->vel_x * state->vel_x + state->vel_y * state->vel_y);
        float res_ax = 0.0f;
        float res_ay = 0.0f;

        if (current_speed > 0.01f) {
            float dir_x = state->vel_x / current_speed;
            float dir_y = state->vel_y / current_speed;

            float a_rr = props->rolling_resistance * GRAVITY_MSS * gravity_scale;
            float viscous_drag = 0.018f * current_speed;
            float total_decel = a_rr + viscous_drag;

            // Prevent friction from reversing velocity in a single sub-step
            if (total_decel * dt_sub > current_speed) {
                total_decel = current_speed / dt_sub;
            }

            res_ax = -dir_x * total_decel;
            res_ay = -dir_y * total_decel;
        }

        // Net acceleration
        state->accel_x = drive_ax + res_ax;
        state->accel_y = drive_ay + res_ay;

        // 4. Velocity integration (Symplectic Euler)
        state->vel_x += state->accel_x * dt_sub;
        state->vel_y += state->accel_y * dt_sub;

        // Static friction deadband when nearly at rest on flat plane
        if (current_speed < 3.5f && fabsf(drive_ax) < 32.0f && fabsf(drive_ay) < 32.0f) {
            state->vel_x = 0.0f;
            state->vel_y = 0.0f;
            state->accel_x = 0.0f;
            state->accel_y = 0.0f;
        }

        // 5. Position integration
        float next_x = state->pos_x + state->vel_x * dt_sub;
        float next_y = state->pos_y + state->vel_y * dt_sub;

        // 6. Circular rim boundary collision detection & restitution
        float dx = next_x - arena->center_x;
        float dy = next_y - arena->center_y;
        float dist = sqrtf(dx * dx + dy * dy);
        float max_dist = arena->arena_radius - props->radius_px;

        if (dist > max_dist) {
            float nx = dx / dist;
            float ny = dy / dist;

            // Unit tangent along perimeter (counter-clockwise)
            float tx = -ny;
            float ty =  nx;

            // Clamp position precisely to circular perimeter
            next_x = arena->center_x + nx * max_dist;
            next_y = arena->center_y + ny * max_dist;

            // Normal and tangential velocity decomposition
            float vn = state->vel_x * nx + state->vel_y * ny;
            float vt = state->vel_x * tx + state->vel_y * ty;

            // Rebound if marble is heading outward into the wall
            if (vn > 0.0f) {
                any_wall_collision = true;
                if (vn > max_impact_speed) {
                    max_impact_speed = vn;
                }

                float vn_rebound = -props->restitution * vn;
                float vt_glide   = (1.0f - props->wall_friction) * vt;

                state->vel_x = vn_rebound * nx + vt_glide * tx;
                state->vel_y = vn_rebound * ny + vt_glide * ty;
            }
        }

        // 7. Accumulate physical roll displacement
        float sub_dx = next_x - state->pos_x;
        float sub_dy = next_y - state->pos_y;
        float sub_dist = sqrtf(sub_dx * sub_dx + sub_dy * sub_dy);

        if (sub_dist > 0.0005f) {
            state->roll_angle_rad += sub_dist / props->radius_px;
            if (state->roll_angle_rad > 6.2831853f) {
                state->roll_angle_rad = fmodf(state->roll_angle_rad, 6.2831853f);
            }
            state->heading_rad = atan2f(sub_dy, sub_dx);
        }

        state->pos_x = next_x;
        state->pos_y = next_y;
    }

    // Record boundary collision status for audio trigger
    state->wall_collided = any_wall_collision;
    if (any_wall_collision) {
        state->last_impact_speed = max_impact_speed;
    }

    // Scalar speeds
    state->speed_px_s = sqrtf(state->vel_x * state->vel_x + state->vel_y * state->vel_y);
    state->speed_mm_s = (state->speed_px_s / props->pixels_per_meter) * 1000.0f;
}
