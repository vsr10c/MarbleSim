# MarbleSim — Stainless Steel Marble Simulator

A high-fluidity, real-time physical simulation of an **AISI 316 Stainless Steel Marble** running at a locked **60 FPS** on the **Waveshare ESP32-S3 Touch AMOLED 1.75C** development board.

Built using **ESP-IDF v6.0.2**, the official **Waveshare Board Support Package (BSP)**, **LVGL v9**, and driven in real time by the onboard **QMI8658 6-axis IMU**, with audible metallic collision sounds produced by the onboard **ES8311 speaker codec**.

---

## ✨ Features

- **AISI 316 Stainless Steel Physics Engine**:
  - **Density & Mass**: Modeled at $8000 \text{ kg/m}^3$ ($8.0 \text{ g/cm}^3$), giving an authentic $11.5 \text{ g}$ heavy feel on a 14 mm diameter sphere ($24 \text{ px}$ radius on a $466 \text{ px}$ circular display).
  - **240 Hz Sub-Stepping**: Runs 4 internal simulation steps per 60 FPS frame ($dt = 4.16 \text{ ms}$), completely eliminating tunneling and enabling smooth circular rim gliding.
  - **Concave Dish Restorative Dynamics**: Subtle radial restorative gradient mimics a precision watch crystal or curved spirit level bowl, settling the marble to center on flat planes.
  - **Rotational Inertia**: Solid sphere moment of inertia factor $\frac{5}{7} \approx 0.7143$ applied to gravitational tilt acceleration ($a = \frac{5}{7} g \sin\theta$).
  - **Rolling Resistance**: $C_{rr} = 0.0035$ for natural coasting and realistic deceleration.
  - **Boundary Restitution**: Strict circular collision handling with $e = 0.68$ elastic rebound restitution and $\mu_{\text{wall}} = 0.12$ tangential contact friction.

- **Audible Metallic Rim Impacts (ES8311 Speaker Codec)**:
  - Procedurally synthesized 46 ms acoustic waveform modeling the modal vibration of solid AISI 316 steel striking the metal casing rim:
    $$s(t) = e^{-t / 0.010} \cdot \left[ 0.55 \sin(2\pi \cdot 2400 t) + 0.30 \sin(2\pi \cdot 4800 t) + 0.15 \sin(2\pi \cdot 7200 t) \right]$$
  - **Dynamic Volume Scaling**: Loudness dynamically scales with collision impact velocity (subtle chime on gentle taps, crisp clack on fast bounces).
  - **40 ms Rim Cooldown**: Prevents buzz or stutter during continuous orbital rim gliding.
  - **Zero 60-FPS Impact**: Audio engine runs asynchronously on **CPU Core 0** via FreeRTOS event queue; Core 1 render loop never waits for I2S audio DMA.

- **AXP2101 Power Button Audio Toggle**:
  - Non-blocking I2C polling of the AXP2101 PMIC PEK (Power Key) short-press status.
  - Short-pressing the power button toggles audio on/off with on-screen visual toast banner: `AUDIO: ON 🔊` (emerald green) or `AUDIO: OFF 🔇` (amber orange).

- **Hyper-Responsive Sensor Pipeline (< 2 ms Latency)**:
  - **Adaptive 1€ (One Euro) Filter**: Automatically modulates cutoff frequency ($f_c = 1.0\text{ Hz} \to 26\text{ Hz}$) based on movement derivative—providing zero resting jitter while eliminating perceptible group delay during quick hand turns.
  - **Gyroscope Rate Lead Compensator**: Injects QMI8658 angular rate feedforward ($\omega_x, \omega_y$) to immediately begin ball acceleration the exact millisecond the wrist turns.

- **Interactive Touch Grab, Drag & Fling**:
  - **Direct Grab**: Touching the marble locks it directly under your finger.
  - **Arena Drag**: Smoothly guide the marble anywhere across the dial face.
  - **Velocity Fling**: Flicking and releasing transfers real fingertip release velocity directly into physical simulation.
  - **Tap Impulse**: Tapping elsewhere pushes the marble with an impulse velocity.

- **Raytraced Metallic Shading**:
  - Procedural Blinn-Phong specular glint, Schlick Fresnel rim reflections ($F_0 = 0.65$), and anisotropic brushed steel grain.
  - Dynamic soft drop shadow with quadratic alpha falloff and tilt-dependent offset rendered onto pure AMOLED blacks (`#000000`).

- **Physical Zero-Calibration & Auto-Centering**:
  - Direct level polling on the onboard **BOOT** button (GPIO 0).
  - Pressing **BOOT** zeros accelerometer bias, **immediately snaps the marble to the center bullseye $(233, 233)$**, and displays `✓ ZEROED & CENTERED` for 2 seconds.
  - Touch calibration: tapping the center bullseye or bottom status prompt also zeroes and centers the marble.

- **Real Gravity Coordinates (1.75C)**:
  - Calibrated specifically for the Waveshare 1.75C aluminum chassis:
    - Tilting right (towards crown button) $\rightarrow +X$ (rolls right).
    - Tilting down (towards 6 o'clock) $\rightarrow +Y$ (rolls down).

- **Interactive Telemetry HUD**:
  - Real-time FPS counter, directional tilt angles (`Tilt: X:%+.1f° Y:%+.1f°`), and speed in $\text{mm/s}$.
  - Tap the top edge of the screen to toggle the HUD overlay.

---

## 🖥️ Hardware Target

| Component | Specification |
| :--- | :--- |
| **Board** | Waveshare ESP32-S3-Touch-AMOLED-1.75C (CNC Aluminum Alloy Case) |
| **MCU** | ESP32-S3R8 (Dual-core Xtensa LX7 @ 240 MHz, 8 MB Octal PSRAM, 16 MB Flash) |
| **Display** | 1.75-inch Circular AMOLED, 466 × 466 pixels, CO5300 QSPI controller |
| **Touch** | CST9217 capacitive touch controller over I2C |
| **Audio Codec** | ES8311 I2S Audio Codec + Onboard Power Amplifier (GPIO 46) & Speaker |
| **IMU** | QMI8658 6-axis IMU (Accelerometer & Gyroscope) over I2C (500 Hz ODR) |
| **PMIC** | AXP2101 power management unit with PEK power key detection |

---

## 🛠️ Build & Flash Instructions

### Prerequisites
- **ESP-IDF v5.3+ or v6.0+** installed (tested on v6.0.2).
- ESP-IDF PowerShell or terminal environment configured.

### 1. Configure the Environment
Activate your ESP-IDF environment (adjust path if needed):

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
```

### 2. Build the Firmware
The ESP-IDF Component Manager will automatically download the official `waveshare/esp32_s3_touch_amoled_1_75`, `waveshare/qmi8658`, and `lvgl/lvgl` components:

```powershell
idf.py build
```

### 3. Flash to the Board
Connect your Waveshare board via USB-C and flash:

```powershell
idf.py -p COM3 flash monitor
```
*(Replace `COM3` with your board's serial port).*

---

## 🎮 Controls & Interactions

- **Tilt the Board**: Marble accelerates and rolls naturally under real gravity with zero perceptible lag.
- **Audible Rim Impacts**: Striking the boundary rim plays a realistic metallic clink from the onboard speaker.
- **Power Button**: Short-press the physical power button to toggle audio ON (`AUDIO: ON 🔊`) / OFF (`AUDIO: OFF 🔇`).
- **Touch Grab & Fling**: Touch directly on the marble to drag it, and flick to launch it across the arena.
- **Tap Impulse**: Tap away from the marble to push it toward the touch point.
- **Recalibrate & Center**: Press the **BOOT** button (or tap the center bullseye) on a level surface. The marble snaps to center and sensor zero-bias is updated.
- **Toggle HUD**: Tap the top edge of the circular screen to hide/show telemetry.

---

## 📄 License

Licensed under the [MIT License](LICENSE).
