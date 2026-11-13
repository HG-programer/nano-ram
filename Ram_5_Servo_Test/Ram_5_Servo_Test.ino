/*
 * =====================================================================================
 * RAM ANIMATRONIC - 5-SECOND AUTOMATIC SEQUENTIAL SERVO TEST RIG
 * =====================================================================================
 * Hardware:
 *   - Arduino Nano (ATmega328P)
 *   - PCA9685 16-Channel 12-Bit PWM Driver (I2C: 0x40)
 *   - XY-3606 / HW-688 5V 5A Buck Converter (powers PCA9685 green terminal)
 *   - 5 Servos plugged into Channels 0, 1, 2, 3, 4
 *
 * How this code works:
 *   - NO SERIAL INPUT REQUIRED! It starts moving automatically on power-up.
 *   - Tests Servo 0 (Head Yaw)   for 5 seconds smoothly (others stay centered).
 *   - Tests Servo 1 (Head Pitch) for 5 seconds smoothly (others stay centered).
 *   - Tests Servo 2 (Waist Tilt) for 5 seconds smoothly (others stay centered).
 *   - Tests Servo 3 (Left Arm)   for 5 seconds smoothly (others stay centered).
 *   - Tests Servo 4 (Right Arm)  for 5 seconds smoothly (others stay centered).
 *   - Pauses 2 seconds, then repeats continuously in an infinite loop!
 *
 * Wire Connections:
 *   Nano A4 (SDA) -> PCA9685 SDA
 *   Nano A5 (SCL) -> PCA9685 SCL
 *   Nano 5V       -> PCA9685 VCC (Logic power)
 *   Nano GND      -> PCA9685 GND
 *   Buck 5V / GND -> PCA9685 Green Screw Terminal (V+ / GND)
 * =====================================================================================
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// Hardware pulse limits for SG90 / MG90S at 50Hz
#define SERVOMIN_TICK  130 // ~0.65ms (~0 deg)
#define SERVOMAX_TICK  610 // ~2.45ms (~180 deg)

const uint8_t NUM_SERVOS = 5;

// Identification for each channel
const char* servoNames[NUM_SERVOS] = {
  "CH 0: Head Yaw (MG90S)   - Turn Left & Right",
  "CH 1: Head Pitch (MG90S) - Tilt Up & Down",
  "CH 2: Waist Tilt (MG90S) - Torso Bow & Lean",
  "CH 3: Left Arm (SG90)    - Shoulder Lift",
  "CH 4: Right Arm (SG90)   - Shoulder Lift & Wave"
};

// Safe motion ranges for testing (protects against hitting physical endstops)
const float minAngles[NUM_SERVOS] = { 50.0,  70.0,  75.0,  20.0,  20.0 };
const float maxAngles[NUM_SERVOS] = { 130.0, 110.0, 105.0, 100.0, 100.0 };
const float CENTER_ANGLE = 90.0;

// Helper to convert Angle (0 - 180) to PCA9685 Ticks
uint16_t angleToPulse(float angle) {
  angle = constrain(angle, 0.0, 180.0);
  return (uint16_t)(SERVOMIN_TICK + (angle / 180.0) * (SERVOMAX_TICK - SERVOMIN_TICK));
}

void setServoAngle(uint8_t ch, float angle) {
  pwm.setPWM(ch, 0, angleToPulse(angle));
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n========================================================"));
  Serial.println(F("   RAM ANIMATRONIC - 5-SECOND AUTOMATIC SERVO TEST RIG  "));
  Serial.println(F("========================================================"));
  Serial.println(F("[BOOT] Initializing PCA9685 PWM Driver..."));

  Wire.begin();
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50); // 50 Hz standard servo frequency

  delay(100);

  // Center all 5 servos initially
  Serial.println(F("[INIT] Locking all 5 servos to 90 deg center..."));
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    setServoAngle(i, CENTER_ANGLE);
  }

  Serial.println(F("[READY] Starting automatic 5-second test cycle in 2 seconds!\n"));
  delay(2000);
}

// Moves one specific servo smoothly for 5 seconds while holding all others at center
void testSingleServo5Seconds(uint8_t servoIdx) {
  Serial.println(F("--------------------------------------------------------"));
  Serial.print(F(">>> NOW TESTING: "));
  Serial.println(servoNames[servoIdx]);
  Serial.println(F("    Running smooth 5.0-second back-and-forth sweep..."));

  unsigned long startTime = millis();
  const unsigned long DURATION_MS = 5000; // Exact 5 seconds
  float minA = minAngles[servoIdx];
  float maxA = maxAngles[servoIdx];

  while (millis() - startTime < DURATION_MS) {
    unsigned long elapsed = millis() - startTime;

    // Smooth sinusoidal sweep: 2 full back-and-forth cycles over the 5 seconds
    // progress completes 2 cycles (4*PI) across 5000ms
    float progress = (float)elapsed / (float)DURATION_MS * 4.0 * PI;
    float normalized = (sin(progress - (PI / 2.0)) + 1.0) / 2.0; // 0.0 to 1.0

    float currentAngle = minA + normalized * (maxA - minA);
    setServoAngle(servoIdx, currentAngle);

    // Ensure all OTHER 4 servos remain locked firmly at 90 deg center
    for (uint8_t other = 0; other < NUM_SERVOS; other++) {
      if (other != servoIdx) {
        setServoAngle(other, CENTER_ANGLE);
      }
    }

    delay(20); // 50 Hz smooth update
  }

  // Return tested servo back to 90 deg center
  setServoAngle(servoIdx, CENTER_ANGLE);
  Serial.println(F("    [OK] Returned to 90 deg."));
  delay(500); // Brief 0.5s pause before moving to next servo
}

void loop() {
  // Sequentially test Servo 0, 1, 2, 3, 4 for 5 seconds each
  for (uint8_t i = 0; i < NUM_SERVOS; i++) {
    testSingleServo5Seconds(i);
  }

  Serial.println(F("\n========================================================"));
  Serial.println(F(">>> CYCLE COMPLETE: All 5 Servos Tested! (Pausing 2s) <<<"));
  Serial.println(F("========================================================\n"));
  delay(2000);
}
