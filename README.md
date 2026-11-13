# Project Nano-Ram (Phase 2): Multimodal Vision-Language-Action Humanoid Animatronic Platform

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Firmware](https://img.shields.io/badge/Firmware-ESP32--S3%20%7C%20C%2B%2B%20%2F%20Arduino-blue.svg)](10_ram_vision_web_companion/)
[![Backend](https://img.shields.io/badge/Cloud%20Backend-Python%20%7C%20FastAPI%20%7C%20Gemini-green.svg)](https://github.com/HG-programer/chibi-ram-backend)
[![Actuation](https://img.shields.io/badge/Kinematics-5--DoF%20%7C%20PCA9685%2012--bit%20PWM-orange.svg)](Ram_Servo_Controller/)
[![ORCID](https://img.shields.io/badge/ORCID-0009--0004--3363--1711-brightgreen.svg)](https://orcid.org/0009-0004-3363-1711)

**Project Nano-Ram** is an open-source undergraduate robotics research project investigating edge-to-cloud **Vision-Language-Action (VLA)** synchronization on resource-constrained embedded hardware. The platform integrates real-time computer vision, neural speech synthesis, conversational reasoning, and closed-loop 5-DoF kinematic control into an expressive animatronic humanoid.

---

## 1. End-to-End System Architecture

```
                                  +------------------------------------+
                                  |        Cloud AI Reasoning          |
                                  |   (Google Gemini Multimodal VLA)   |
                                  +-----------------+------------------+
                                                    ^
                                                    | REST API (Audio / Visuals)
                                                    v
                                  +-----------------+------------------+
                                  |        chibi-ram-backend           |
                                  |   - Fish Audio Neural Voice (TTS)  |
                                  |   - Fast Token Auth & State Cache  |
                                  +-----------------+------------------+
                                                    ^
                                                    | Wi-Fi (HTTP / WebSockets)
                                                    v
 +--------------------------------------------------+--------------------------------------------------+
 |                            Edge Platform (ESP32-S3 Dual-Core)                                      |
 |                                                                                                    |
 |   [OV3660 Camera]  --->  Camera DMA Framebuffer (JPEG Streaming / Face Analysis)                   |
 |   [ES7210 Mic]     --->  I2S ADC (16kHz 16-bit Mono Audio Capture in PSRAM)                        |
 |   [ES8311 DAC]     <---  I2S Audio Output (Synchronized Neural Voice Playback)                     |
 |   [Web Companion]  <---> Local Embedded UI & Real-Time Parameter Tuning (ram_web_app.h)           |
 +--------------------------------------------------+--------------------------------------------------+
                                                    | I2C (SDA / SCL)
                                                    v
 +--------------------------------------------------+--------------------------------------------------+
 |                       5-DoF Actuation Subsystem (PCA9685 12-Bit PWM)                                |
 |                                                                                                    |
 |   CH 0: Head Yaw       (MG90S Metal Gear)  --> Safe: 45° to 135°  | Neutral: 90°                    |
 |   CH 1: Head Pitch     (MG90S Metal Gear)  --> Safe: 65° to 115°  | Neutral: 90°                    |
 |   CH 2: Waist Tilt     (MG90S Metal Gear)  --> Safe: 75° to 105°  | Neutral: 90°                    |
 |   CH 3: Left Arm       (SG90 Micro Servo)  --> Safe: 15° to 110°  | Neutral: 20°                    |
 |   CH 4: Right Arm      (SG90 Micro Servo)  --> Safe: 15° to 120°  | Neutral: 20°                    |
 +--------------------------------------------------+--------------------------------------------------+
                                                    ^
                                                    | 5V High-Current Rail (5A)
                                  +-----------------+------------------+
                                  |    XY-3606 DC-DC Buck Converter    |
                                  +------------------------------------+
```

---

## 2. Repository Layout

```
├── 10_ram_vision_web_companion/    # Core ESP32-S3 edge perception, audio, & web companion firmware
│   ├── 10_ram_vision_web_companion.ino
│   ├── board_config.h, camera_pins.h
│   ├── es7210.cpp, es7210.h        # ES7210 2-Channel I2S ADC Microphone Driver
│   ├── es8311.cpp, es8311.h        # ES8311 Low-Power I2S Audio DAC Driver
│   ├── ram_web_app.h               # Embedded HTML/JS dashboard & diagnostic controls
│   ├── secrets.example.h           # Credential template for Wi-Fi & backend authentication
│   └── ESP32-S3-CAM-XXXX-schematic.pdf
├── Ram_Servo_Controller/           # 5-DoF Kinematics Controller with interactive serial commands
│   └── Ram_Servo_Controller.ino
├── Ram_5_Servo_Test/               # Autonomous hardware cycling & burn-in test rig
│   └── Ram_5_Servo_Test.ino
├── Hardware_CAD/                   # 21 Watertight, Production-Audited STL CAD Models
│   ├── Production_Shells/          # Parts 01-11: Head, Torso, Collar, Apron, Sleeves, Skirt & Lid
│   │   └── ORDER_INSTRUCTIONS_ROBU.txt
│   └── Internal_Skeleton/          # Parts 12-21: Bearing sleeves, yaw/pitch gimbals, & trays
├── Computational_Geometry_Audit/   # Automated Python geometric & mechanical fit audit pipeline
│   ├── audit_geometry.py           # Standalone binary STL parser (numpy-based, zero CAD lock-in)
│   ├── run_computational_geometry_audit.py
│   └── computational_geometry_audit_report.md
├── chibi-ram-backend/              # Submodule: Cloud multimodal backend (Gemini & Fish Audio)
├── .gitignore                      # Security rules ignoring real credentials, secrets, & caches
├── LICENSE                         # MIT Open-Source License
└── README.md
```

---

## 3. Subsystem Breakdown

### A. Edge Perception & Web Companion (`10_ram_vision_web_companion/`)
* **Processor:** ESP32-S3 (Xtensa Dual-Core 32-bit LX7, 240 MHz, 8 MB PSRAM).
* **Vision Pipeline:** OV3660 camera streaming real-time video and snapshot uploads for multimodal cloud analysis.
* **Acoustic Subsystem:**
  * **Input:** ES7210 2-channel I2S ADC recording at 16 kHz 16-bit mono into DMA PSRAM buffers.
  * **Output:** ES8311 low-power audio codec driving onboard speaker with atomic MP3 streaming.
* **Embedded UI:** Local web dashboard served directly from ESP32 flash for manual motion overrides, camera preview, and network health monitoring.

### B. 5-DoF Kinematic Control (`Ram_Servo_Controller/`)
* **PWM Generation:** PCA9685 I2C 16-channel 12-bit PWM generator operating at 50 Hz carrier frequency (4.88 µs timing resolution).
* **Power Distribution:** Servo power is isolated via an XY-3606 / HW-688 5V 5A DC-DC buck converter to eliminate microcontroller brownouts under multi-servo stall conditions.
* **Serial Command Protocol (115200 baud):**
  * `c` : Centers all actuators to neutral calibration angles for mechanical horn alignment.
  * `1` : Executes maid greeting bow routine (*"Okaerinasaimase"*).
  * `2` : Executes friendly waving gesture with expressive head tilt.
  * `3` : Autonomous environmental scanning and head look-around.
  * `4` : Continuous sinusoidal idle breathing kinematics across torso and neck.
  * `s <channel> <degrees>` : Direct single-channel actuation.

### C. Cloud Reasoning Backend (`chibi-ram-backend`)
* Source code repository: [`HG-programer/chibi-ram-backend`](https://github.com/HG-programer/chibi-ram-backend).
* Deployed on Render (`chibi-ram-backend.onrender.com`).
* Connects Google Gemini Multimodal APIs with neural speech generation from Fish Audio, returning synchronized speech and actuation tags.

### D. Computational Geometry & Mechanical Fit Audit (`Computational_Geometry_Audit/`)
All 21 mechanical parts undergo algorithmic validation via automated Python scripts before 3D printing:
1. **Watertight Manifold Check:** 100% of production parts have 0 non-manifold edges.
2. **Wall Thickness Verification:** Minimum shell wall thickness $\ge 1.0\text{ mm}$ across artistic curves.
3. **Skeleton Clearance:** Outward shell thickening ($\Delta \approx +0.5\text{ mm}$) ensures internal clearance $\ge 0.235\text{ mm}$ around all 10 skeletal mechanisms.
4. **Mating Seam Tolerance:** Clamshell tolerance verified at $0.23 - 0.25\text{ mm}$ to prevent friction binding.

---

## 4. Hardware Pinout & Wiring

| Arduino / ESP32-S3 | PCA9685 PWM Driver | Notes |
| :--- | :--- | :--- |
| **SDA** (GPIO / A4) | **SDA** | I2C Data line (Address: `0x40`) |
| **SCL** (GPIO / A5) | **SCL** | I2C Clock line |
| **5V** | **VCC** | Logic power only |
| **GND** | **GND** | Shared common ground |

| XY-3606 5V 5A Buck | PCA9685 Terminal | Notes |
| :--- | :--- | :--- |
| **5V (+)** | **V+** (Green Terminal) | High-current power for 5 servos |
| **GND (-)** | **GND** (Green Terminal) | High-current return |

> **Warning:** Never power servos from the microcontroller's 5V/3.3V linear regulator. Stall current will cause instantaneous reset or hardware damage.

---

## 5. Getting Started

### 1. Configure Edge Firmware
1. Navigate to `10_ram_vision_web_companion/`.
2. Copy `secrets.example.h` to `secrets.h`:
   ```bash
   cp secrets.example.h secrets.h
   ```
3. Open `secrets.h` and configure your Wi-Fi credentials (`SECRET_WIFI_SSID`, `SECRET_WIFI_PASS`) and backend URL.
4. Flash the firmware to your ESP32-S3 via the Arduino IDE or ESP-IDF.

### 2. Configure Servo Controller
1. Open `Ram_Servo_Controller/Ram_Servo_Controller.ino` in the Arduino IDE.
2. Ensure the `Adafruit_PWMServoDriver` and `Wire` libraries are installed.
3. Upload to your controller and open the Serial Monitor at **115200 baud**.
4. Type `c` to center all servos before attaching kinematic horns.

### 3. Run Geometric Validation Tests
```bash
cd Computational_Geometry_Audit
python -m venv .venv
source .venv/bin/activate  # On Windows: .venv\Scripts\activate
pip install numpy trimesh scipy
python run_computational_geometry_audit.py
```

---

## 6. Author & Academic Context

* **Researcher:** Harshit Gupta  
* **Affiliation:** Maryland Institute of Technology and Management (Affiliated with Jharkhand University of Technology)  
* **Field:** Undergraduate Research, B.Tech in Computer Science & Engineering  
* **ORCID:** [0009-0004-3363-1711](https://orcid.org/0009-0004-3363-1711)  
* **GitHub Profile:** [@HG-programer](https://github.com/HG-programer)  
* **Primary Contact:** `harshitg933@gmail.com`

---

## 7. License

This repository is licensed under the [MIT License](LICENSE).
