#include "marble_physics.h"
#include <math.h>
#include <string.h>

#define GRAVITY_MSS 9.80665f

void marble_physics_get_default_properties(marble_properties_t *props) {
    if (!props) return;
    props->density_kg_m3      = 8000.0f;    // AISI 316 Stainless Steel
    props->diameter_mm        = 14.0f;      // 14mm ball bearing
    props->radius_px          = 24.0f;      // 48px diameter on 466x466 display
    props->mass_kg            = 0.0115f;    // 11.5 grams
    props->rolling_factor     = 5.0f / 7.0f;// 0.7142857f for solid sphere inertia
    props->rolling_resistance = 0.0040f;    // Natural rolling resistance on smooth glass/dish
    props->restitution        = 0.65f;      // Solid metallic bounce (not perpetual rubber bouncing)
    props->wall_friction      = 0.15f;      // Tangential velocity loss at wall
    props->pixels_per_meter   = 13700.0f;
}

void marble_physics_get_default_arena(arena_config_t *arena) {
    if (!arena) return;
    arena->center_x     = 233.0f;
    arena->center_y     = 233.0f;
    arena->arena_radius = 228.0f; // Leaves 5px margin to edge of 466px screen
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

    state->wall_collided = false;

    // 1. Gravity acceleration along screen surface with solid sphere rolling factor (5/7)
    // Scale factor converts m/s^2 into screen px/s^2 calibrated for intuitive handheld motion
    const float gravity_scale = 1500.0f;
    float drive_ax = tilt_accel_x * gravity_scale * props->rolling_factor;
    float drive_ay = tilt_accel_y * gravity_scale * props->rolling_factor;

    // 2. Compute current scalar speed
    float current_speed = sqrtf(state->vel_x * state->vel_x + state->vel_y * state->vel_y);

    // 3. Rolling resistance opposes current velocity
    float res_ax = 0.0f;
    float res_ay = 0.0f;
    if (current_speed > 0.01f) {
        float dir_x = state->vel_x / current_speed;
        float dir_y = state->vel_y / current_speed;

        // Rolling resistance deceleration: a_rr = Crr * g * scale
        float a_rr = props->rolling_resistance * GRAVITY_MSS * gravity_scale;
        
        // Small aerodynamic & surface viscous drag
        float viscous_drag = 0.020f * current_speed;

        float total_decel = a_rr + viscous_drag;

        // Ensure friction doesn't cause velocity reversal in a single step
        if (total_decel * dt_seconds > current_speed) {
            total_decel = current_speed / dt_seconds;
        }

        res_ax = -dir_x * total_decel;
        res_ay = -dir_y * total_decel;
    }

    // Total acceleration
    state->accel_x = drive_ax + res_ax;
    state->accel_y = drive_ay + res_ay;

    // 4. Velocity integration (Symplectic Euler)
    state->vel_x += state->accel_x * dt_seconds;
    state->vel_y += state->accel_y * dt_seconds;

    // Static friction deadband: if moving very slowly and tilt is negligible, bring to full rest
    if (current_speed < 4.0f && fabsf(drive_ax) < 35.0f && fabsf(drive_ay) < 35.0f) {
        state->vel_x = 0.0f;
        state->vel_y = 0.0f;
        state->accel_x = 0.0f;
        state->accel_y = 0.0f;
    }

    // 5. Position integration
    float new_x = state->pos_x + state->vel_x * dt_seconds;
    float new_y = state->pos_y + state->vel_y * dt_seconds;

    // 6. Circular boundary collision detection and restitution
    float dx = new_x - arena->center_x;
    float dy = new_y - arena->center_y;
    float dist = sqrtf(dx * dx + dy * dy);

    float max_dist = arena->arena_radius - props->radius_px;
    if (max_dist < 10.0f) max_dist = 10.0f;

    if (dist > max_dist) {
        // Interpenetration detected
        state->wall_collided = true;

        // Normal unit vector pointing outward from arena center
        float nx = dx / dist;
        float ny = dy / dist;

        // Tangent unit vector
        float tx = -ny;
        float ty = nx;

        // Clamp position back to exact perimeter
        new_x = arena->center_x + nx * max_dist;
        new_y = arena->center_y + ny * max_dist;

        // Decompose velocity into normal and tangential components
        float vn = state->vel_x * nx + state->vel_y * ny;
        float vt = state->vel_x * tx + state->vel_y * ty;

        // Record impact speed for haptics / visual flash
        state->last_impact_speed = fabsf(vn);

        // Only reflect if velocity is pointing outward toward the wall
        if (vn > 0.0f) {
            float vn_rebound = -props->restitution * vn;
            float vt_rebound = (1.0f - props->wall_friction) * vt;

            state->vel_x = vn_rebound * nx + vt_rebound * tx;
            state->vel_y = vn_rebound * ny + vt_rebound * ty;
        }
    }

    // 7. Update angular rolling displacement
    float step_dx = new_x - state->pos_x;
    float step_dy = new_y - state->pos_y;
    float step_dist = sqrtf(step_dx * step_dx + step_dy * step_dy);

    if (step_dist > 0.001f) {
        state->roll_angle_rad += step_dist / props->radius_px;
        // Keep within 0 to 2*PI
        if (state->roll_angle_rad > 6.2831853f) {
            state->roll_angle_rad = fmodf(state->roll_angle_rad, 6.2831853f);
        }
        state->heading_rad = atan2f(step_dy, step_dx);
    }

    // Commit new position
    state->pos_x = new_x;
    state->pos_y = new_y;

    // Update speed metrics
    state->speed_px_s = sqrtf(state->vel_x * state->vel_x + state->vel_y * state->vel_y);
    state->speed_mm_s = state->speed_px_s / (props->pixels_per_meter / 1000.0f);
}
