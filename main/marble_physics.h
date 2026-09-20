#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Physical properties of AISI 316 Stainless Steel Marble
 */
typedef struct {
    float density_kg_m3;       ///< Density (8000 kg/m^3 for AISI 316)
    float diameter_mm;         ///< Real-world diameter (~14.0 mm)
    float mass_kg;             ///< Calculated mass (~0.0115 kg = 11.5 g)
    float radius_px;           ///< Display radius in pixels (24 px, 48 px diameter)
    float rolling_factor;      ///< 5/7 = ~0.7143 for solid sphere rotational inertia
    float rolling_resistance;  ///< Crr = 0.0025
    float restitution;         ///< Coefficient of restitution (0.85 = high elasticity)
    float wall_friction;       ///< Tangential friction during wall bounce (0.10)
    float pixels_per_meter;    ///< Scale factor converting m/s^2 to px/s^2 (~13700 px/m)
} marble_properties_t;

/**
 * @brief Dynamic simulation state of the marble
 */
typedef struct {
    float pos_x;               ///< Current X position (pixels, screen coordinates)
    float pos_y;               ///< Current Y position (pixels, screen coordinates)
    float vel_x;               ///< Velocity X (pixels / second)
    float vel_y;               ///< Velocity Y (pixels / second)
    float accel_x;             ///< Current linear acceleration X (pixels / s^2)
    float accel_y;             ///< Current linear acceleration Y (pixels / s^2)
    float roll_angle_rad;      ///< Cumulative rolling angle (radians)
    float heading_rad;         ///< Direction of motion (radians)
    float speed_px_s;          ///< Scalar speed (pixels / second)
    float speed_mm_s;          ///< Scalar speed (mm / second)
    float last_impact_speed;   ///< Speed of last wall bounce
    bool  wall_collided;       ///< Set true on frame when boundary impact occurs
} marble_state_t;

/**
 * @brief Boundary configuration for circular display arena
 */
typedef struct {
    float center_x;            ///< Center X of circular screen (233.0 px)
    float center_y;            ///< Center Y of circular screen (233.0 px)
    float arena_radius;        ///< Radius of outer arena perimeter (228.0 px)
} arena_config_t;

/**
 * @brief Initialize default AISI 316 stainless steel properties
 */
void marble_physics_get_default_properties(marble_properties_t *props);

/**
 * @brief Initialize default arena geometry (466x466 circular AMOLED)
 */
void marble_physics_get_default_arena(arena_config_t *arena);

/**
 * @brief Reset marble state to center or specific position
 */
void marble_physics_init_state(marble_state_t *state, float init_x, float init_y);

/**
 * @brief Advance marble physics simulation by time dt
 * 
 * @param state Pointer to marble dynamic state
 * @param props Physical material properties
 * @param arena Circular arena geometry
 * @param tilt_accel_x Raw tilt acceleration X in m/s^2 (aligned to screen X)
 * @param tilt_accel_y Raw tilt acceleration Y in m/s^2 (aligned to screen Y)
 * @param dt_seconds Time step in seconds (e.g. 0.01666f for 60 FPS)
 */
void marble_physics_update(marble_state_t *state,
                           const marble_properties_t *props,
                           const arena_config_t *arena,
                           float tilt_accel_x,
                           float tilt_accel_y,
                           float dt_seconds);

/**
 * @brief Apply external impulse force (e.g. touch flick/tap)
 */
void marble_physics_apply_impulse(marble_state_t *state, float impulse_vx, float impulse_vy);

#ifdef __cplusplus
}
#endif
