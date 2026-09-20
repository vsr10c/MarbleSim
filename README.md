# MarbleSim — Stainless Steel Marble Simulator

A real-time physical simulation of an **AISI 316 Stainless Steel Marble** running at **50 to 60 FPS** on the **Waveshare ESP32-S3 Touch AMOLED 1.75C** development board.

Built using **ESP-IDF v6.0.2**, the official **Waveshare Board Support Package (BSP)**, **LVGL v9**, and driven in real time by the onboard **QMI8658 6-axis IMU**.

---

## ✨ Features

- **AISI 316 Stainless Steel Physics Engine**:
  - **Density & Mass**: Modeled at $8000 \text{ kg/m}^3$ ($8.0 \text{ g/cm}^3$), giving an authentic $11.5 \text{ g}$ heavy feel on a 14 mm diameter sphere ($24 \text{ px}$ radius on a $466 \text{ px}$ circular display).
  - **Rotational Inertia**: Solid sphere moment of inertia factor $\frac{5}{7} \approx 0.7143$ applied to gravitational tilt acceleration ($a = \frac{5}{7} g \sin\theta$).
  - **Rolling Resistance**: $C_{rr} = 0.0040$ for natural coasting and realistic deceleration.
  - **Boundary Restitution**: Strict circular collision handling with $e = 0.65$ elastic rebound restitution and $\mu_{\text{wall}} = 0.15$ tangential contact friction.
  - **Static Friction Threshold**: Marble stays motionless when the board is level on a flat surface.
- **Raytraced Metallic Shading**:
  - Procedurally generated Blinn-Phong specular glint, Schlick Fresnel rim reflections ($F_0 = 0.65$), and anisotropic brushed steel grain.
  - Dynamic soft drop shadow with quadratic alpha falloff rendered beneath the marble onto pure AMOLED blacks (`#000000`).
- **Smooth 60 FPS Graphics**:
  - Partial dirty-region invalidation limits redraws to the $48 \times 48 \text{ px}$ bounding box ($< 1 \text{ ms}$ QSPI transfer).
  - Pinned simulation task running on ESP32-S3 CPU Core 1 @ 240 MHz.
- **Physical Zero-Calibration & Auto-Centering**:
  - Direct level polling on the onboard **BOOT** button (GPIO 0).
  - Pressing **BOOT** zeros accelerometer bias, **immediately snaps the marble to the center bullseye $(233, 233)$**, and displays `✓ ZEROED & CENTERED` for 2 seconds.
  - Touch calibration: tapping the center bullseye or bottom prompt on the touchscreen also zeroes and centers the marble.
- **Real Gravity Coordinates (1.75C)**:
  - Calibrated specifically for the Waveshare 1.75C aluminum chassis:
    - Tilting right (towards crown button) $\rightarrow +X$ (rolls right).
    - Tilting down (towards 6 o'clock) $\rightarrow +Y$ (rolls down).
- **Interactive Telemetry HUD**:
  - Real-time FPS counter, directional tilt angles (`Tilt: X:%+.1f° Y:%+.1f°`), and speed in $\text{mm/s}$.
  - Tap the top edge of the screen to toggle the HUD overlay.
  - Tap near the marble to flick it with an impulse velocity.

---

## 🖥️ Hardware Target

| Component | Specification |
| :--- | :--- |
| **Board** | Waveshare ESP32-S3-Touch-AMOLED-1.75C (CNC Aluminum Alloy Case) |
| **MCU** | ESP32-S3R8 (Dual-core Xtensa LX7 @ 240 MHz, 8 MB Octal PSRAM, 16 MB Flash) |
| **Display** | 1.75-inch Circular AMOLED, 466 × 466 pixels, CO5300 QSPI controller |
| **Touch** | CST9217 capacitive touch controller over I2C |
| **IMU** | QMI8658 6-axis IMU (Accelerometer & Gyroscope) over I2C (500 Hz ODR) |
| **PMIC** | AXP2101 power management unit |

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

- **Tilt the Board**: Marble accelerates and rolls naturally under real gravity.
- **Hit the Rim**: Marble bounces realistically off the circular boundary ($e = 0.65$).
- **Flick Marble**: Tap anywhere near the marble on the touchscreen to push it with an impulse velocity.
- **Recalibrate & Center**: Press the **BOOT** button (or tap the center bullseye) on a level surface. The marble snaps to center and sensor zero-bias is updated.
- **Toggle HUD**: Tap the top edge of the circular screen.

---

## 📄 License

Licensed under the [MIT License](LICENSE).
