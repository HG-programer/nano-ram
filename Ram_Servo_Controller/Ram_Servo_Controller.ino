/*
 * =====================================================================================
 * RAM ANIMATRONIC - 5-DOF SERVO CONTROLLER & BENCH TEST RIG
 * =====================================================================================
 * Hardware:
 *   - Arduino Nano (ATmega328P)
 *   - PCA9685 16-Channel 12-Bit PWM Driver (I2C Address: 0x40)
 *   - XY-3606 / HW-688 5V 5A DC-DC Buck Converter (Servo Power)
 *   - 3x MG90S Metal Gear Micro Servos (Head Yaw, Head Pitch, Waist Tilt)
 *   - 2x SG90 Micro Servos (Left Arm, Right Arm)
 *
 * Wire Connections:
 *   Arduino Nano  -->  PCA9685 Logic Header
 *     A4 (SDA)    -->  SDA
 *     A5 (SCL)    -->  SCL
 *     5V          -->  VCC  (Logic ONLY)
 *     GND         -->  GND
 *
 *   XY-3606 Buck  -->  PCA9685 Power Terminal
 *     5V Out (+)  -->  V+   (Green Screw Terminal)
 *     GND Out (-) -->  GND  (Green Screw Terminal)
 *
 * Channel Mapping:
 *   CH 0: Head Yaw (MG90S)       - Left/Right Turn      [Safe: 45° to 135°, Neutral: 90°]
 *   CH 1: Head Pitch (MG90S)     - Up/Down Tilt         [Safe: 65° to 115°, Neutral: 90°]
 *   CH 2: Waist Tilt (MG90S)     - Torso Bow/Lean       [Safe: 75° to 105°, Neutral: 90°]
 *   CH 3: Left Arm (SG90)        - Arm Lift/Gesture     [Safe: 15° to 110°, Neutral: 20°]
 *   CH 4: Right Arm (SG90)       - Wave/Gesture         [Safe: 15° to 120°, Neutral: 20°]
 *
 * Serial Commands (at 115200 baud):
 *   'c' : ALL SERVOS TO CALIBRATION NEUTRAL (Mount your servo horns in this mode!)
 *   '1' : Maid Greeting Bow ("Okaerinasaimase")
 *   '2' : Friendly Wave & Head Tilt
 *   '3' : Head Look-Around / Scanning
 *   '4' : Continuous Organic Idle Breathing
 *   's <ch> <deg>' : Move specific channel (e.g. "s 0 90")
 * =====================================================================================
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// Hardware Calibration constants for SG90 / MG90S at 50Hz (20ms period)
// 12-bit timer has 4096 counts per 20ms (4.88us per tick)
#define SERVOMIN_TICK  130 // ~0.65ms pulse (~0 deg)
#define SERVOMAX_TICK  610 // ~2.45ms pulse (~180 deg)

// Servo Channel Definitions
const uint8_t CH_HEAD_YAW   = 0;
const uint8_t CH_HEAD_PITCH = 1;
const uint8_t CH_WAIST_TILT = 2;
const uint8_t CH_ARM_LEFT   = 3;
const uint8_t CH_ARM_RIGHT  = 4;

const uint8_t NUM_SERVOS = 5;

// Structural Joint Safety Limits (Clamped to protect 3D printed mechanical endstops)
struct ServoJoint {
  const char* name;
  uint8_t channel;
  float minAngle;
  float maxAngle;
  float neutralAngle;
  float currentAngle;
  float targetAngle;
  float speed; // degrees per update step
};

ServoJoint joints[NUM_SERVOS] = {
  {"Head Yaw (L/R)",     CH_HEAD_YAW,   45.0, 135.0,  90.0,  90.0,  90.0, 2.0},
  {"Head Pitch (U/D)",   CH_HEAD_PITCH, 65.0, 115.0,  90.0,  90.0,  90.0, 1.8},
  {"Waist Tilt (Bow)",   CH_WAIST_TILT, 75.0, 105.0,  90.0,  90.0,  90.0, 1.2},
  {"Left Arm",           CH_ARM_LEFT,   15.0, 110.0,  20.0,  20.0,  20.0, 2.5},
  {"Right Arm (Wave)",   CH_ARM_RIGHT,  15.0, 120.0,  20.0,  20.0,  20.0, 2.5}
};

// Operating Modes
enum AnimMode {
  MODE_IDLE,
  MODE_CALIBRATE,
  MODE_BOW,
  MODE_WAVE,
  MODE_LOOK_AROUND,
  MODE_BREATHING
};

AnimMode currentMode = MODE_CALIBRATE;
unsigned long lastUpdate = 0;
unsigned long stateTimer = 0;
uint8_t animStep = 0;

// Convert Angle (0 - 180) to PCA9685 Tick Count (0 - 4095)
uint16_t angleToPulse(float angle) {
  angle = constrain(angle, 0.0, 180.0);
  return (uint16_t)(SERVOMIN_TICK + (angle / 180.0) * (SERVOMAX_TICK - SERVOMIN_TICK));
}

// Set target angle with mechanical boundary clamping
void setJointTarget(uint8_t index, float target, float speed = 0.0) {
  if (index >= NUM_SERVOS) return;
  target = constrain(target, joints[index].minAngle, joints[index].maxAngle);
  joints[index].targetAngle = target;
  if (speed > 0.0) joints[index].speed = speed;
}

// Non-blocking smooth motion updater (called every loop tick)
void updateServosSmooth() {
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    if (joints[i].currentAngle < joints[i].targetAngle) {
      joints[i].currentAngle += joints[i].speed;
      if (joints[i].currentAngle > joints[i].targetAngle) {
        joints[i].currentAngle = joints[i].targetAngle;
      }
    } else if (joints[i].currentAngle > joints[i].targetAngle) {
      joints[i].currentAngle -= joints[i].speed;
      if (joints[i].currentAngle < joints[i].targetAngle) {
        joints[i].currentAngle = joints[i].targetAngle;
      }
    }
    pwm.setPWM(joints[i].channel, 0, angleToPulse(joints[i].currentAngle));
  }
}

// Check if all joints have reached their targets
bool jointsAtTarget() {
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    if (abs(joints[i].currentAngle - joints[i].targetAngle) > 0.5) {
      return false;
    }
  }
  return true;
}

// Jump directly to neutral positions
void resetToNeutral() {
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    setJointTarget(i, joints[i].neutralAngle, 2.0);
  }
}

void printMenu() {
  Serial.println(F("\n========================================================"));
  Serial.println(F("           RAM ANIMATRONIC 5-DOF CONTROLLER             "));
  Serial.println(F("========================================================"));
  Serial.println(F("Available Commands:"));
  Serial.println(F("  'c' -> CALIBRATION MODE (All servos hold 90 neutral)"));
  Serial.println(F("  '1' -> Maid Greeting Bow ('Okaerinasaimase')"));
  Serial.println(F("  '2' -> Friendly Greeting Wave & Head Tilt"));
  Serial.println(F("  '3' -> Head Scanning / Look Around"));
  Serial.println(F("  '4' -> Organic Life-Like Breathing Loop"));
  Serial.println(F("  's <ch> <deg>' -> Set single channel (e.g. 's 0 90')"));
  Serial.println(F("========================================================"));
  Serial.print(F("Current Mode: "));
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000); // Wait for serial console

  Serial.println(F("\n[BOOT] Initializing PCA9685 I2C Driver..."));
  Wire.begin();

  pwm.begin();
  pwm.setOscillatorFrequency(27000000); // Default 27MHz internal clock
  pwm.setPWMFreq(50); // 50 Hz standard servo frequency

  delay(50);

  // Default to Calibration mode on boot so horns can be safely mounted
  currentMode = MODE_CALIBRATE;
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    joints[i].currentAngle = joints[i].neutralAngle;
    joints[i].targetAngle  = joints[i].neutralAngle;
    pwm.setPWM(joints[i].channel, 0, angleToPulse(joints[i].currentAngle));
  }

  printMenu();
  Serial.println(F("CALIBRATE (All servos holding neutral 90deg)"));
}

void handleSerialInput() {
  if (!Serial.available()) return;

  char cmd = Serial.read();

  if (cmd == 'c' || cmd == 'C') {
    currentMode = MODE_CALIBRATE;
    resetToNeutral();
    Serial.println(F("\n[CMD] Switched to CALIBRATION MODE (Mount horns now at 90 deg)"));
  }
  else if (cmd == '1') {
    currentMode = MODE_BOW;
    animStep = 0;
    stateTimer = millis();
    Serial.println(F("\n[CMD] Playing MAID GREETING BOW"));
  }
  else if (cmd == '2') {
    currentMode = MODE_WAVE;
    animStep = 0;
    stateTimer = millis();
    Serial.println(F("\n[CMD] Playing FRIENDLY WAVE"));
  }
  else if (cmd == '3') {
    currentMode = MODE_LOOK_AROUND;
    animStep = 0;
    stateTimer = millis();
    Serial.println(F("\n[CMD] Playing HEAD SCAN / LOOK AROUND"));
  }
  else if (cmd == '4') {
    currentMode = MODE_BREATHING;
    animStep = 0;
    stateTimer = millis();
    Serial.println(F("\n[CMD] Running CONTINUOUS BREATHING LOOP"));
  }
  else if (cmd == 's' || cmd == 'S') {
    // Manual single channel override: "s <ch> <deg>"
    int ch = Serial.parseInt();
    float deg = Serial.parseFloat();
    if (ch >= 0 && ch < NUM_SERVOS) {
      currentMode = MODE_IDLE;
      setJointTarget(ch, deg, 3.0);
      Serial.print(F("[MANUAL] Set CH "));
      Serial.print(ch);
      Serial.print(F(" ("));
      Serial.print(joints[ch].name);
      Serial.print(F(") to "));
      Serial.print(deg);
      Serial.println(F(" deg"));
    }
  }
}

// Animation State Machine
void updateAnimationStateMachine() {
  unsigned long now = millis();

  switch (currentMode) {
    case MODE_CALIBRATE:
      // Holds neutral angles continuously
      break;

    case MODE_BOW:
      // Formal Maid Bow sequence
      if (animStep == 0) {
        // Prepare: arms gracefully to apron, head level
        setJointTarget(CH_ARM_LEFT, 35.0, 1.5);
        setJointTarget(CH_ARM_RIGHT, 35.0, 1.5);
        setJointTarget(CH_HEAD_YAW, 90.0, 1.5);
        animStep = 1;
        stateTimer = now;
      } else if (animStep == 1 && now - stateTimer > 600) {
        // Bow forward: waist tilts down (80 deg), head tilts modestly down (75 deg)
        setJointTarget(CH_WAIST_TILT, 78.0, 0.8);
        setJointTarget(CH_HEAD_PITCH, 75.0, 0.8);
        animStep = 2;
        stateTimer = now;
      } else if (animStep == 2 && now - stateTimer > 1800) {
        // Hold bow, then rise gracefully back to upright
        setJointTarget(CH_WAIST_TILT, 90.0, 0.6);
        setJointTarget(CH_HEAD_PITCH, 90.0, 0.6);
        animStep = 3;
        stateTimer = now;
      } else if (animStep == 3 && now - stateTimer > 1500) {
        // Return arms to rest
        resetToNeutral();
        currentMode = MODE_IDLE;
        Serial.println(F("[ANIM] Maid Bow complete."));
      }
      break;

    case MODE_WAVE:
      // Maid waving sequence
      if (animStep == 0) {
        // Head tilts slightly curious, right arm lifts to wave height
        setJointTarget(CH_HEAD_YAW, 80.0, 1.5);
        setJointTarget(CH_HEAD_PITCH, 95.0, 1.2);
        setJointTarget(CH_ARM_RIGHT, 95.0, 3.0);
        animStep = 1;
        stateTimer = now;
      } else if (animStep == 1 && now - stateTimer > 700) {
        // Wave back and forth
        setJointTarget(CH_ARM_RIGHT, 75.0, 3.5);
        animStep = 2;
        stateTimer = now;
      } else if (animStep == 2 && now - stateTimer > 350) {
        setJointTarget(CH_ARM_RIGHT, 105.0, 3.5);
        animStep = 3;
        stateTimer = now;
      } else if (animStep == 3 && now - stateTimer > 350) {
        setJointTarget(CH_ARM_RIGHT, 80.0, 3.5);
        animStep = 4;
        stateTimer = now;
      } else if (animStep == 4 && now - stateTimer > 400) {
        // Return to neutral
        resetToNeutral();
        currentMode = MODE_IDLE;
        Serial.println(F("[ANIM] Wave complete."));
      }
      break;

    case MODE_LOOK_AROUND:
      // Curious scanning sequence
      if (animStep == 0) {
        // Turn head left, slight upward pitch
        setJointTarget(CH_HEAD_YAW, 120.0, 1.2);
        setJointTarget(CH_HEAD_PITCH, 98.0, 1.0);
        animStep = 1;
        stateTimer = now;
      } else if (animStep == 1 && now - stateTimer > 1400) {
        // Turn head right, slight tilt
        setJointTarget(CH_HEAD_YAW, 60.0, 1.0);
        setJointTarget(CH_HEAD_PITCH, 85.0, 0.8);
        animStep = 2;
        stateTimer = now;
      } else if (animStep == 2 && now - stateTimer > 1800) {
        // Center head
        resetToNeutral();
        currentMode = MODE_IDLE;
        Serial.println(F("[ANIM] Look-around complete."));
      }
      break;

    case MODE_BREATHING:
      // Organic subtle life-like swaying
      if (animStep == 0) {
        setJointTarget(CH_WAIST_TILT, 92.0, 0.2);
        setJointTarget(CH_HEAD_PITCH, 92.0, 0.2);
        animStep = 1;
        stateTimer = now;
      } else if (animStep == 1 && now - stateTimer > 2500) {
        setJointTarget(CH_WAIST_TILT, 88.0, 0.2);
        setJointTarget(CH_HEAD_PITCH, 88.0, 0.2);
        animStep = 0;
        stateTimer = now;
      }
      break;

    case MODE_IDLE:
    default:
      break;
  }
}

void loop() {
  handleSerialInput();

  // Run servo smoothing updates every 20ms (50Hz)
  if (millis() - lastUpdate >= 20) {
    lastUpdate = millis();
    updateAnimationStateMachine();
    updateServosSmooth();
  }
}
