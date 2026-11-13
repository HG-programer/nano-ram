#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <lwip/dns.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "FS.h"
#include "SD_MMC.h"
#include "ESP_I2S.h"
#include "Audio.h"

// Camera BSP
#include "esp_camera.h"
#include "img_converters.h"
#include "board_config.h"

// Camera JPEG Frame Container
struct JpegFrame {
  camera_fb_t *fb;
  uint8_t     *buf;
  size_t       len;
  bool         allocated;
};

JpegFrame get_camera_jpeg(uint8_t quality = 80);
void release_camera_jpeg(JpegFrame &frame);

// Waveshare Audio & IO Drivers
#include "esp_check.h"
#include "es7210.h"
#include "es8311.h"
#include "io_extension.h"

// Secrets & Configuration
#include "secrets.h"

// Embedded Web App
#include "ram_web_app.h"

static const char *TAG = "chibi_ram_companion";

// User Configuration from secrets.h
const char* ssid             = SECRET_WIFI_SSID;
const char* password         = SECRET_WIFI_PASS;
const char* backend_base_url = SECRET_BACKEND_URL;
const char* chat_api_token   = SECRET_CHAT_TOKEN;

// Recording parameters
#define RECORD_SAMPLE_RATE     (16000)
#define RECORD_CHANNELS        (1)       // 16kHz Mono WAV (downmixed from dual mics for 2x upload speed)
#define RECORD_BITS_PER_SAMPLE (16)
#define RECORD_DURATION_SEC    (5)

// 350 KB buffer in PSRAM handles up to 10 seconds of 16kHz 16-bit Mono audio + 44B WAV header
#define MAX_PSRAM_REC_SIZE     (350 * 1024)

// Hardware Audio Pins
#define I2S_MCLK_PIN  GPIO_NUM_10
#define I2S_BCK_PIN   GPIO_NUM_11
#define I2S_LRCK_PIN  GPIO_NUM_12
#define I2S_DIN_PIN   GPIO_NUM_13   // Dual mic data from ES7210 ADC
#define I2S_DOUT_PIN  GPIO_NUM_14   // Speaker data to ES8311 DAC

#define I2C_SDA_PIN   GPIO_NUM_8
#define I2C_SCL_PIN   GPIO_NUM_7

// SD_MMC 1-Bit Mode Pins
const int sd_clk = 16;
const int sd_cmd = 43;
const int sd_d0  = 44;

#define ES7210_I2C_ADDR        (0x40)
#define ES7210_ADC_VOLUME      (6)       // +6 dB digital volume (clean headroom)
#define DEFAULT_SPEAKER_VOLUME (75)      // 0 dB unity gain on ES8311

// =========================================================================
// PCA9685 16-CHANNEL SERVO DRIVER (I2C ADDRESS 0x41)
// =========================================================================
// NOTE: Address 0x41 requires bridging solder pad 'A0' on PCA9685 breakout
// to avoid colliding with ES7210 microphone ADC on 0x40!
#define PCA9685_I2C_ADDR       (0x41)
#define PCA9685_MODE1          (0x00)
#define PCA9685_PRESCALE       (0xFE)
#define PCA9685_LED0_ON_L      (0x06)

// Servo Channels
#define SERVO_CH_PAN           (0)  // Neck Pan (Left/Right)
#define SERVO_CH_TILT          (1)  // Head Tilt (Up/Down)
#define SERVO_CH_ROLL          (2)  // Head Roll / Ear Tilt (Side-to-Side)
#define SERVO_CH_ARM           (3)  // Arm (Wave)

// Servo Pulse Range (Standard 50Hz, 20ms period, 500µs - 2500µs)
#define SERVO_US_MIN           (500)
#define SERVO_US_MAX           (2500)
#define SERVO_US_CENTER        (1500)

bool pca9685_available = false;

// Animatronic Servo State
float servo_current_us[4] = {1500.0f, 1500.0f, 1500.0f, 1500.0f};
float servo_target_us[4]  = {1500.0f, 1500.0f, 1500.0f, 1500.0f};

enum MotionType {
  MOTION_IDLE,
  MOTION_TILT,
  MOTION_NOD,
  MOTION_SHAKE,
  MOTION_WAVE,
  MOTION_BOW
};

MotionType active_motion = MOTION_IDLE;
uint32_t motion_start_time = 0;
String current_motion_name = "IDLE";

// =========================================================================
// GLOBAL OBJECTS & STATE
// =========================================================================
static es7210_dev_handle_t es7210_handle = NULL;
static es8311_handle_t     es8311_handle = NULL;
I2SClass i2s;
Audio *active_player = nullptr;
WebServer server(80);
Preferences ram_soul; // ESP32 built-in NVS persistent memory (Soul)

// In-Memory Audio Recording Buffer (PSRAM)
uint8_t *psram_rec_buf = nullptr;
size_t   psram_rec_len = 0;

bool camera_available   = false;
bool sd_card_available  = false;
String current_state    = "idle"; // "idle", "recording", "thinking", "speaking"
String last_transcript  = "How is the weather today";
String last_reply       = "ハル様、お呼びでしょうか。ラムはいつでもお側におりますわ。";
String last_identity    = "NOT_ENROLLED"; // "HARU", "STRANGER", "NO_FACE", "NOT_ENROLLED"
float  last_similarity  = 0.0f;
int current_volume      = DEFAULT_SPEAKER_VOLUME;
int last_played_seq     = 0;

bool is_recording = false;
bool is_playing   = false;
bool pending_look_and_react = false;
bool pending_record         = false;
bool pending_snap_roast     = false;
bool pending_replay         = false;
String pending_say_message  = "";

// =========================================================================
// 🧠 EMOTION ENGINE, AUTONOMY & ENVIRONMENTAL AWARENESS
// =========================================================================
int ram_mood = 65;               // 0 (Cold/Depressed) to 100 (Ecstatic/Devoted)
uint8_t last_ambient_lux = 120;  // 0 (Pitch black) to 255 (Bright)
uint32_t last_interaction_time   = 0;
uint32_t last_env_check_time     = 0;
uint32_t last_boredom_check_time = 0;
bool morning_greeting_sent       = false;

// Forward declarations
bool record_voice(uint32_t seconds);
bool save_snapshot(const char *path);
void upload_voice_and_chat();
void upload_vision_and_chat();
void upload_snap_roast();
void send_text_chat(const String &msg);
bool check_and_enroll_sd_haru();
bool enroll_camera_haru();
bool download_ram_speech(const char *local_path);
void play_saved_speech(const char *local_path);
void play_cloud_speech(const char *url);
void fetch_and_play_ram_speech();
bool wait_and_play_ram_reply(uint32_t max_wait_ms = 60000);
bool apply_backend_reply(const String &response);
String escapeJson(const String &str);
bool pca9685_init();
void pca9685_set_servo_us(uint8_t channel, uint16_t us);
void trigger_motion(const String &tag);
void update_servo_gestures();
void check_ambient_light();
void update_autonomy_and_mood();
void set_mood(int new_mood);

// =========================================================================
// 💾 PERSISTENT MEMORY (SOUL) HELPER: WEAR-LEVELED NVS WRITER
// =========================================================================
void set_mood(int new_mood) {
  int clamped = constrain(new_mood, 0, 100);
  // Only write to flash if the mood actually changed (avoids unnecessary write cycles)
  if (clamped != ram_mood) {
    ram_mood = clamped;
    ram_soul.putInt("mood", ram_mood);
    Serial.printf("[SOUL] 💾 Mood updated and saved to flash NVS: %d%%\n", ram_mood);
  }
}

// =========================================================================
// JSON STRING ESCAPE HELPER
// =========================================================================
String escapeJson(const String &str) {
  String out = "";
  out.reserve(str.length() + 16);
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else out += c;
  }
  return out;
}

// =========================================================================
// PCA9685 DIRECT I2C DRIVER (NO EXTERNAL LIBRARY REQUIRED)
// =========================================================================
void pca9685_write8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t pca9685_read8(uint8_t reg) {
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)PCA9685_I2C_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

void pca9685_set_pwm(uint8_t channel, uint16_t on, uint16_t off) {
  if (!pca9685_available || channel > 15) return;
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  Wire.write(PCA9685_LED0_ON_L + 4 * channel);
  Wire.write((uint8_t)(on & 0xFF));
  Wire.write((uint8_t)((on >> 8) & 0xFF));
  Wire.write((uint8_t)(off & 0xFF));
  Wire.write((uint8_t)((off >> 8) & 0xFF));
  Wire.endTransmission();
}

void pca9685_set_servo_us(uint8_t channel, uint16_t us) {
  if (!pca9685_available) return;
  us = constrain(us, SERVO_US_MIN, SERVO_US_MAX);
  // 50Hz = 20,000 µs period. 4096 ticks per 20,000 µs -> ticks = (us * 4096) / 20000
  uint16_t ticks = (uint32_t)us * 4096 / 20000;
  pca9685_set_pwm(channel, 0, ticks);
}

bool pca9685_init() {
  Wire.beginTransmission(PCA9685_I2C_ADDR);
  byte err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[PCA9685] Device NOT detected at 0x41 (I2C error %d).\n", err);
    Serial.println("[PCA9685] Ensure solder jumper 'A0' is bridged to shift from 0x40 to 0x41!");
    Serial.println("[PCA9685] Running in headless animatronic mode (servos disabled).");
    pca9685_available = false;
    return false;
  }

  // Set MODE1 to sleep (bit 4) to allow prescaler setting
  pca9685_write8(PCA9685_MODE1, 0x10);
  // 50Hz PWM: Prescale = round(25,000,000 / (4096 * 50)) - 1 = 121 (0x79)
  pca9685_write8(PCA9685_PRESCALE, 121);
  // Set MODE2 to totem pole (0x04) for active servo drive
  pca9685_write8(0x01, 0x04);
  // Wake up and enable Auto-Increment (bit 5)
  pca9685_write8(PCA9685_MODE1, 0x20);
  delay(5);
  // Restart PWM
  pca9685_write8(PCA9685_MODE1, 0xA0);

  pca9685_available = true;
  Serial.println("[PCA9685] Servo driver initialized successfully at 0x41 (50Hz PWM)!");

  // Move channels 0..3 to neutral center
  for (uint8_t ch = 0; ch < 4; ch++) {
    pca9685_set_servo_us(ch, SERVO_US_CENTER);
    servo_current_us[ch] = SERVO_US_CENTER;
    servo_target_us[ch]  = SERVO_US_CENTER;
  }
  return true;
}

// =========================================================================
// ANIMATRONIC GESTURES & SMOOTH EASING STATE MACHINE
// =========================================================================
void trigger_motion(const String &tag) {
  String upper = tag;
  upper.toUpperCase();
  upper.trim();

  if (upper == "TILT") {
    active_motion = MOTION_TILT;
  } else if (upper == "NOD") {
    active_motion = MOTION_NOD;
  } else if (upper == "SHAKE") {
    active_motion = MOTION_SHAKE;
  } else if (upper == "WAVE") {
    active_motion = MOTION_WAVE;
  } else if (upper == "BOW") {
    active_motion = MOTION_BOW;
  } else {
    active_motion = MOTION_IDLE;
  }

  current_motion_name = upper;
  motion_start_time = millis();
  Serial.printf("[SERVO] Triggering animatronic motion: %s\n", current_motion_name.c_str());
}

void update_servo_gestures() {
  static uint32_t last_servo_update = 0;
  uint32_t now = millis();
  if (now - last_servo_update < 20) return; // Smooth 50Hz update rate
  last_servo_update = now;

  uint32_t elapsed = now - motion_start_time;

  switch (active_motion) {
    case MOTION_TILT: {
      // Cute anime head tilt: tilts head roll (Ch 2) to 1250us, pauses, returns to center
      if (elapsed < 600) {
        servo_target_us[SERVO_CH_ROLL] = 1250.0f; // Tilt right
        servo_target_us[SERVO_CH_PAN]  = 1550.0f; // Slight gaze shift
      } else if (elapsed < 2000) {
        servo_target_us[SERVO_CH_ROLL] = 1250.0f; // Hold cute tilt
      } else if (elapsed < 2600) {
        servo_target_us[SERVO_CH_ROLL] = 1500.0f; // Return to center
        servo_target_us[SERVO_CH_PAN]  = 1500.0f;
      } else {
        active_motion = MOTION_IDLE;
        current_motion_name = "IDLE";
      }
      break;
    }
    case MOTION_NOD: {
      // Affirmative / thoughtful nod: Pitch down, up, down, up
      if (elapsed < 350) {
        servo_target_us[SERVO_CH_TILT] = 1320.0f; // Dip head down
      } else if (elapsed < 700) {
        servo_target_us[SERVO_CH_TILT] = 1550.0f; // Come up slightly
      } else if (elapsed < 1050) {
        servo_target_us[SERVO_CH_TILT] = 1350.0f; // Second gentle nod
      } else if (elapsed < 1400) {
        servo_target_us[SERVO_CH_TILT] = 1500.0f; // Back to level
      } else {
        active_motion = MOTION_IDLE;
        current_motion_name = "IDLE";
      }
      break;
    }
    case MOTION_SHAKE: {
      // Tsundere disapproval head shake: Pan left-right-left-center
      if (elapsed < 300) {
        servo_target_us[SERVO_CH_PAN] = 1350.0f; // Turn left
      } else if (elapsed < 600) {
        servo_target_us[SERVO_CH_PAN] = 1650.0f; // Turn right
      } else if (elapsed < 900) {
        servo_target_us[SERVO_CH_PAN] = 1380.0f; // Turn left
      } else if (elapsed < 1200) {
        servo_target_us[SERVO_CH_PAN] = 1500.0f; // Center
      } else {
        active_motion = MOTION_IDLE;
        current_motion_name = "IDLE";
      }
      break;
    }
    case MOTION_WAVE: {
      // Arm wave greeting: Arm Ch 3 cycles up and down
      if (elapsed < 400) {
        servo_target_us[SERVO_CH_ARM] = 1900.0f;
      } else if (elapsed < 800) {
        servo_target_us[SERVO_CH_ARM] = 1200.0f;
      } else if (elapsed < 1200) {
        servo_target_us[SERVO_CH_ARM] = 1900.0f;
      } else if (elapsed < 1600) {
        servo_target_us[SERVO_CH_ARM] = 1500.0f;
      } else {
        active_motion = MOTION_IDLE;
        current_motion_name = "IDLE";
      }
      break;
    }
    case MOTION_BOW: {
      // Respectful Maid Bow to Haru-sama: Neck/torso dips forward 15°-20°, pauses, returns gracefully
      if (elapsed < 600) {
        servo_target_us[SERVO_CH_TILT] = 1260.0f; // Smooth dip forward
        servo_target_us[SERVO_CH_PAN]  = 1500.0f; // Keep aligned forward
        servo_target_us[SERVO_CH_ROLL] = 1500.0f;
      } else if (elapsed < 1400) {
        servo_target_us[SERVO_CH_TILT] = 1260.0f; // Hold respectful maid bow
      } else if (elapsed < 2000) {
        servo_target_us[SERVO_CH_TILT] = 1500.0f; // Rise gracefully back to level
      } else {
        active_motion = MOTION_IDLE;
        current_motion_name = "IDLE";
      }
      break;
    }
    case MOTION_IDLE:
    default: {
      // Gentle alive breathing / micro-movement when speaking or idle
      if (is_playing) {
        // Subtle micro-pitch variation synced with speech
        float breath = sin((float)(now % 2000) * (2.0f * 3.14159f / 2000.0f)) * 40.0f;
        servo_target_us[SERVO_CH_TILT] = 1500.0f + breath;
        servo_target_us[SERVO_CH_PAN]  = 1500.0f; // Re-center PAN if prior gesture interrupted
        servo_target_us[SERVO_CH_ROLL] = 1500.0f; // Re-center ROLL
        servo_target_us[SERVO_CH_ARM]  = 1500.0f; // Re-center ARM
      } else {
        servo_target_us[SERVO_CH_PAN]  = 1500.0f;
        servo_target_us[SERVO_CH_TILT] = 1500.0f;
        servo_target_us[SERVO_CH_ROLL] = 1500.0f;
        servo_target_us[SERVO_CH_ARM]  = 1500.0f;
      }
      break;
    }
  }

  // If servo driver hardware is absent, state machine still maintains correct UI state above
  if (!pca9685_available) return;

  // Smooth Exponential Easing: dynamically scales with Ram's mood!
  float easing_factor = 0.20f;
  if (ram_mood > 80) {
    easing_factor = 0.35f; // Fast, bouncy, eager movements when ecstatic/devoted
  } else if (ram_mood < 25) {
    easing_factor = 0.08f; // Slow, heavy, reluctant movements when lonely/cold
  }

  for (uint8_t ch = 0; ch < 4; ch++) {
    float diff = servo_target_us[ch] - servo_current_us[ch];
    if (abs(diff) > 1.0f) {
      servo_current_us[ch] += diff * easing_factor;
      pca9685_set_servo_us(ch, (uint16_t)round(servo_current_us[ch]));
    }
  }
}

// =========================================================================
// ES7210 MICROPHONE ADC INITIALIZATION
// =========================================================================
static esp_err_t es7210_mic_init(void) {
  es7210_i2c_config_t es7210_i2c_conf = {
    .i2c_port = I2C_NUM_0,
    .i2c_addr = ES7210_I2C_ADDR
  };
  
  esp_err_t ret = es7210_new_codec(&es7210_i2c_conf, &es7210_handle);
  if (ret != ESP_OK) {
    Serial.printf("[ES7210] new_codec failed: %d\n", ret);
    return ret;
  }

  es7210_codec_config_t codec_conf = {};
  codec_conf.i2s_format     = ES7210_I2S_FMT_I2S;
  codec_conf.mclk_ratio     = 256;
  codec_conf.sample_rate_hz = RECORD_SAMPLE_RATE;
  codec_conf.bit_width      = ES7210_I2S_BITS_16B;
  codec_conf.mic_bias       = ES7210_MIC_BIAS_2V87;
  codec_conf.mic_gain       = ES7210_MIC_GAIN_30DB; // Clean +30 dB preamp gain
  codec_conf.flags.tdm_enable = false;

  ret = es7210_config_codec(es7210_handle, &codec_conf);
  if (ret != ESP_OK) {
    Serial.printf("[ES7210] config_codec failed: %d\n", ret);
    return ret;
  }

  ret = es7210_config_volume(es7210_handle, ES7210_ADC_VOLUME);
  if (ret != ESP_OK) {
    Serial.printf("[ES7210] config_volume failed: %d\n", ret);
    return ret;
  }

  Serial.println("[ES7210] Dual Microphone ADC initialized successfully!");
  return ESP_OK;
}

// =========================================================================
// ES8311 SPEAKER DAC INITIALIZATION
// =========================================================================
static esp_err_t es8311_speaker_init(void) {
  es8311_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (!es8311_handle) {
    Serial.println("[ES8311 ERROR] es8311_create failed!");
    return ESP_FAIL;
  }

  const es8311_clock_config_t es_clk = {
    .mclk_inverted = false,
    .sclk_inverted = false,
    .mclk_from_mclk_pin = true,
    .mclk_frequency = (44100 * 256),
    .sample_frequency = 44100
  };

  esp_err_t err = es8311_init(es8311_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
  if (err != ESP_OK) {
    Serial.printf("[ES8311 ERROR] es8311_init failed: %d\n", err);
    return err;
  }

  es8311_voice_volume_set(es8311_handle, current_volume, NULL);
  es8311_microphone_config(es8311_handle, false);

  Serial.println("[ES8311] Speaker DAC initialized successfully!");
  return ESP_OK;
}

// =========================================================================
// SD CARD INITIALIZATION
// =========================================================================
static bool sdcard_init(void) {
  SD_MMC.end();
  delay(50);
  if (!SD_MMC.setPins(sd_clk, sd_cmd, sd_d0)) {
    Serial.println("[SD_MMC] Pin assignment failed!");
    sd_card_available = false;
    return false;
  }
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (SD_MMC.begin("/sdcard", true)) {
      Serial.printf("[SD_MMC] Card mounted: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
      sd_card_available = true;
      return true;
    }
    SD_MMC.end();
    delay(120);
  }
  Serial.println("[SD_MMC] Card Mount Failed! Running in PSRAM in-memory mode.");
  sd_card_available = false;
  return false;
}

// =========================================================================
// CAMERA JPEG CONVERTER & FRAME LIFECYCLE HELPER
// =========================================================================
JpegFrame get_camera_jpeg(uint8_t quality) {
  JpegFrame result = { nullptr, nullptr, 0, false };
  if (!camera_available) return result;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAMERA ERROR] Failed to grab frame from sensor!");
    return result;
  }
  result.fb = fb;

  if (fb->format == PIXFORMAT_JPEG) {
    result.buf = fb->buf;
    result.len = fb->len;
    result.allocated = false;
  } else {
    // Sensor is in RGB565 / YUV422: dynamically compress to JPEG
    uint8_t *out_buf = nullptr;
    size_t   out_len = 0;
    if (frame2jpg(fb, quality, &out_buf, &out_len)) {
      result.buf = out_buf;
      result.len = out_len;
      result.allocated = true;
    } else {
      Serial.println("[CAMERA ERROR] frame2jpg compression failed!");
    }
  }
  return result;
}

void release_camera_jpeg(JpegFrame &frame) {
  if (frame.allocated && frame.buf) {
    free(frame.buf);
    frame.buf = nullptr;
  }
  if (frame.fb) {
    esp_camera_fb_return(frame.fb);
    frame.fb = nullptr;
  }
}

// =========================================================================
// OV3660 CAMERA INITIALIZATION
// =========================================================================
static bool camera_init() {
  Serial.println("[CAMERA] Powering up and probing camera sensor...");

  // Ensure camera LDO power is ON via IO expander
  IO_EXTENSION_Output(IO_EXTENSION_IO_6, 1);
  delay(50);

  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size   = FRAMESIZE_QVGA; // 320x240 for snappy streaming
  config.pixel_format = PIXFORMAT_RGB565; // OV3660 native format
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.fb_count     = 1;
  config.sccb_i2c_port = I2C_NUM_0;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK && psramFound()) {
    Serial.printf("[CAMERA] PSRAM RGB565 init returned 0x%x. Retrying with DRAM...\n", err);
    esp_camera_deinit();
    delay(50);
    config.fb_location = CAMERA_FB_IN_DRAM;
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK) {
    Serial.printf("[CAMERA ERROR] Camera probe failed with code 0x%x. Deinitializing camera.\n", err);
    esp_camera_deinit();
    // If esp_camera_deinit() deleted I2C driver on port 0, re-initialize Wire cleanly
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
    delay(10);
    IO_EXTENSION_Init();
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1); // Guarantee microphone ADC power is ON!
    IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1); // Guarantee speaker PA power is ON
    if (es8311_handle) {
      es8311_voice_volume_set(es8311_handle, current_volume, NULL);
    }
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);    // Match standard mounting orientation
    s->set_hmirror(s, 0);
  }

  // CRITICAL FIX: esp_camera_init configures low-level SCCB on port 0 which leaves Arduino Wire
  // in an invalid state, causing subsequent Wire.endTransmission() calls to fail with error 4.
  // Re-initialise Wire cleanly now that camera registers are configured:
  Wire.end();
  delay(15);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 100000);
  delay(15);
  DEV_I2C_Init();
  IO_EXTENSION_Init();
  IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1); // Guarantee microphone ADC power is ON!
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1); // Guarantee speaker PA (NS4150) power is ON!
  IO_EXTENSION_Output(IO_EXTENSION_IO_6, 1); // Keep camera LDO power ON!
  if (es8311_handle) {
    es8311_voice_volume_set(es8311_handle, current_volume, NULL);
  }

  Serial.println("[CAMERA] Camera initialized successfully!");
  return true;
}

// =========================================================================
// SNAPSHOT HELPER
// =========================================================================
bool save_snapshot(const char *path) {
  if (!camera_available) {
    Serial.println("[CAMERA] Camera not available, skipping snapshot.");
    return false;
  }

  JpegFrame frame = get_camera_jpeg(80);
  if (!frame.buf || frame.len == 0) {
    Serial.println("[CAMERA ERROR] Failed to grab JPEG frame!");
    release_camera_jpeg(frame);
    return false;
  }

  if (!sd_card_available) {
    release_camera_jpeg(frame);
    return true; // Frame acquired in memory successfully
  }

  File file = SD_MMC.open(path, FILE_WRITE);
  if (!file) {
    Serial.printf("[WARN] Failed to open %s for writing on SD card\n", path);
    release_camera_jpeg(frame);
    return false;
  }
  file.write(frame.buf, frame.len);
  file.close();
  release_camera_jpeg(frame);
  Serial.printf("[CAMERA] Snapshot saved to %s (%u bytes)\n", path, frame.len);
  return true;
}

// =========================================================================
// WI-FI & MDNS INITIALIZATION
// =========================================================================
void connect_to_wifi() {
  Serial.print("\n[Wi-Fi] Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // Disable modem sleep to prevent latency spikes and audio jitter
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected successfully!");
    Serial.printf("[Wi-Fi] IP Address: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[Wi-Fi] Signal RSSI: %d dBm\n", WiFi.RSSI());

    // Configure robust public DNS (Google 8.8.8.8 & Cloudflare 1.1.1.1) to avoid ISP DNS timeouts
    ip_addr_t primary_dns, secondary_dns;
    primary_dns.type = IPADDR_TYPE_V4;
    primary_dns.u_addr.ip4.addr = ESP_IP4TOADDR(8, 8, 8, 8);
    secondary_dns.type = IPADDR_TYPE_V4;
    secondary_dns.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    dns_setserver(0, &primary_dns);
    dns_setserver(1, &secondary_dns);

    // Verify DNS resolution to backend
    IPAddress backend_ip;
    String backend_host = String(backend_base_url);
    backend_host.replace("https://", "");
    backend_host.replace("http://", "");
    int slash_idx = backend_host.indexOf('/');
    if (slash_idx >= 0) backend_host = backend_host.substring(0, slash_idx);

    Serial.printf("[DNS] Resolving %s...\n", backend_host.c_str());
    if (WiFi.hostByName(backend_host.c_str(), backend_ip)) {
      Serial.printf("[DNS] Resolved %s -> %s\n", backend_host.c_str(), backend_ip.toString().c_str());
    } else {
      Serial.printf("[DNS ERROR] Failed to resolve %s! Retrying...\n", backend_host.c_str());
      delay(500);
      if (WiFi.hostByName(backend_host.c_str(), backend_ip)) {
        Serial.printf("[DNS] Resolved on retry: %s -> %s\n", backend_host.c_str(), backend_ip.toString().c_str());
      }
    }

    if (MDNS.begin("chibi-ram")) {
      Serial.println("[mDNS] Responder started: http://chibi-ram.local");
      MDNS.addService("http", "tcp", 80);
    }

    // 12. Sync internal real-time clock via NTP (IST: UTC+5:30 for Haru-sama)
    configTime(5 * 3600 + 1800, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "IST-5:30", 1);
    tzset();
    Serial.println("[NTP] Internal clock synced to Indian Standard Time (IST, UTC+5:30).");
  } else {
    Serial.println("\n[Wi-Fi] Connection failed! Check credentials in secrets.h.");
  }
}

// =========================================================================
// WEBSERVER HANDLERS
// =========================================================================
void setup_web_server() {
  // 1. Root page (PWA Web App)
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE_INDEX);
  });

  // 2. Live camera snapshot (/cam.jpg)
  server.on("/cam.jpg", HTTP_GET, []() {
    if (is_recording || is_playing) {
      server.send(503, "text/plain", "busy");
      return;
    }
    if (!camera_available) {
      server.send(503, "text/plain", "Camera not connected");
      return;
    }
    JpegFrame frame = get_camera_jpeg(75);
    if (!frame.buf || frame.len == 0) {
      release_camera_jpeg(frame);
      server.send(503, "text/plain", "Camera capture failed");
      return;
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.setContentLength(frame.len);
    server.send(200, "image/jpeg", "");
    WiFiClient client = server.client();
    client.write(frame.buf, frame.len);
    release_camera_jpeg(frame);
  });

  // 3. Status JSON (/api/status) with proper JSON escaping
  server.on("/api/status", HTTP_GET, []() {
    String json = "{";
    json += "\"state\":\"" + current_state + "\",";
    json += "\"camera_available\":" + String(camera_available ? "true" : "false") + ",";
    json += "\"pca9685_available\":" + String(pca9685_available ? "true" : "false") + ",";
    json += "\"last_motion\":\"" + escapeJson(current_motion_name) + "\",";
    json += "\"last_transcript\":\"" + escapeJson(last_transcript) + "\",";
    json += "\"last_reply\":\"" + escapeJson(last_reply) + "\",";
    json += "\"identity\":\"" + escapeJson(last_identity) + "\",";
    json += "\"similarity\":" + String(last_similarity, 2) + ",";
    json += "\"seq\":" + String(last_played_seq) + ",";
    json += "\"volume\":" + String(current_volume) + ",";
    json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
    json += "\"ram_mood\":" + String(ram_mood) + ",";
    json += "\"ambient_lux\":" + String(last_ambient_lux) + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // 3b. Enroll Haru's face (/api/enroll)
  server.on("/api/enroll", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    bool ok = check_and_enroll_sd_haru();
    if (!ok && camera_available) {
      ok = enroll_camera_haru();
    }
    if (ok) {
      server.send(200, "application/json", "{\"ok\":true,\"message\":\"Haru-sama's face enrolled successfully!\"}");
    } else {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"Enrollment failed. Ensure face is clearly visible or haru.jpg is on SD card.\"}");
    }
  });

  // 4. Action triggers (/api/action) parsed with ArduinoJson
  server.on("/api/action", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    const char *action = doc["action"] | "";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");

    if (strcmp(action, "look_and_react") == 0) {
      pending_look_and_react = true;
    } else if (strcmp(action, "snap_roast") == 0) {
      pending_snap_roast = true;
    } else if (strcmp(action, "record") == 0) {
      pending_record = true;
    } else if (strcmp(action, "replay") == 0) {
      pending_replay = true;
    } else if (strcmp(action, "stop") == 0) {
      if (active_player) {
        active_player->stopSong();
      }
      current_state = "idle";
    }
  });

  // 5. Trigger Motion (/api/motion)
  server.on("/api/motion", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    const char *tag = doc["motion"] | "";
    if (*tag) {
      trigger_motion(tag);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true,\"motion\":\"" + escapeJson(tag) + "\"}");
  });

  // 6. Direct text prompt (/api/say)
  server.on("/api/say", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Missing body\"}");
      return;
    }
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    const char *msg = doc["message"] | "";
    if (*msg) {
      pending_say_message = String(msg);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  // 7. Volume slider (/api/volume) with type check to prevent 0-mute on malformed JSON
  server.on("/api/volume", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      JsonDocument doc;
      if (!deserializeJson(doc, body) && doc["volume"].is<int>()) {
        int new_vol = doc["volume"].as<int>();
        if (new_vol >= 0 && new_vol <= 100) {
          current_volume = new_vol;
          if (es8311_handle) {
            es8311_voice_volume_set(es8311_handle, current_volume, NULL);
          }
          if (active_player) {
            active_player->setVolume(map(current_volume, 0, 100, 0, 21));
          }
          Serial.printf("[Audio] Volume updated to %d%%\n", current_volume);
        }
      }
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
  Serial.println("[WebServer] Controller listening on port 80");
}

// =========================================================================
// IN-MEMORY WAV HEADER WRITER (PSRAM)
// =========================================================================
void write_wav_header_mem(uint8_t *buf, uint32_t sampleRate, uint16_t bitsPerSample, uint16_t channels, uint32_t dataSize) {
  uint32_t fileSize = dataSize + 36;
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);

  uint8_t header[44] = {
    'R', 'I', 'F', 'F',
    (uint8_t)(fileSize & 0xff), (uint8_t)((fileSize >> 8) & 0xff), (uint8_t)((fileSize >> 16) & 0xff), (uint8_t)((fileSize >> 24) & 0xff),
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0,
    1, 0,
    (uint8_t)(channels & 0xff), (uint8_t)((channels >> 8) & 0xff),
    (uint8_t)(sampleRate & 0xff), (uint8_t)((sampleRate >> 8) & 0xff), (uint8_t)((sampleRate >> 16) & 0xff), (uint8_t)((sampleRate >> 24) & 0xff),
    (uint8_t)(byteRate & 0xff), (uint8_t)((byteRate >> 8) & 0xff), (uint8_t)((byteRate >> 16) & 0xff), (uint8_t)((byteRate >> 24) & 0xff),
    (uint8_t)(blockAlign & 0xff), (uint8_t)((blockAlign >> 8) & 0xff),
    (uint8_t)(bitsPerSample & 0xff), (uint8_t)((bitsPerSample >> 8) & 0xff),
    'd', 'a', 't', 'a',
    (uint8_t)(dataSize & 0xff), (uint8_t)((dataSize >> 8) & 0xff), (uint8_t)((dataSize >> 16) & 0xff), (uint8_t)((dataSize >> 24) & 0xff)
  };

  memcpy(buf, header, 44);
}

// =========================================================================
// RECORD AUDIO FROM MICROPHONES DIRECTLY TO PSRAM
// =========================================================================
bool record_voice(uint32_t seconds) {
  if (!psram_rec_buf) {
    Serial.println("[ERROR] PSRAM recording buffer not available!");
    return false;
  }

  is_recording = true;
  current_state = "recording";
  psram_rec_len = 0;

  i2s.setPins(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  i2s.setTimeout(1000);
  if (!i2s.begin(I2S_MODE_STD, RECORD_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("[FATAL] Failed to initialize I2S for recording!");
    is_recording = false;
    current_state = "idle";
    return false;
  }

  Serial.println("\n[RECORD] Starting in-memory recording (16kHz Mono, 250Hz HPF + Adaptive VAD)...");
  Serial.println("[RECORD] Speak clearly toward Ram's microphones!");

  // Reserve first 44 bytes for WAV header, audio samples start at offset 44
  uint32_t mono_bytes_written = 0;

  const size_t stereo_buffer_size = 2048; // 512 stereo samples (L: 2B, R: 2B)
  int16_t *stereo_buffer = (int16_t *)malloc(stereo_buffer_size);
  int16_t *mono_buffer   = (int16_t *)malloc(stereo_buffer_size / 2); // 512 mono samples (1024B)
  if (!stereo_buffer || !mono_buffer) {
    if (stereo_buffer) free(stereo_buffer);
    if (mono_buffer) free(mono_buffer);
    i2s.end();
    is_recording = false;
    current_state = "idle";
    return false;
  }

  // VAD & Filter Parameters
  const uint32_t SILENCE_TIMEOUT_MS  = 750;   // Truncate silence 750ms after speech ceases
  const uint32_t MIN_RECORD_MS       = 900;   // Guarantee at least 900ms recording window
  const uint32_t MAX_WAIT_SPEECH_MS  = 3500;  // Abort if no speech begins within 3.5s
  const uint32_t MAX_TOTAL_RECORD_MS = seconds * 1000;

  // IIR High-Pass Filter state (~250 Hz cutoff @ 16 kHz: alpha = 0.906 ~= 928/1024)
  int16_t prev_raw = 0;
  int32_t prev_filtered = 0;

  // Adaptive Noise Floor Calibration
  uint32_t noise_floor = 400; // Initial baseline in quiet room
  uint32_t chunks_analyzed = 0;

  bool speech_detected = false;
  uint32_t record_start_time = millis();
  uint32_t speech_start_time = 0;
  uint32_t last_speech_time  = 0;

  uint32_t max_stereo_bytes = RECORD_SAMPLE_RATE * 2 * (RECORD_BITS_PER_SAMPLE / 8) * seconds;
  uint32_t stereo_bytes_read = 0;

  while (stereo_bytes_read < max_stereo_bytes) {
    size_t bytes_to_read = min(stereo_buffer_size, (size_t)(max_stereo_bytes - stereo_bytes_read));
    size_t bytes_read = i2s.readBytes((char *)stereo_buffer, bytes_to_read);

    if (bytes_read > 0) {
      size_t num_frames = bytes_read / 4;
      int16_t min_sample = 32767;
      int16_t max_sample = -32768;

      for (size_t i = 0; i < num_frames; i++) {
        // Use primary MIC1 (Left channel) directly to eliminate dual-mic differential phase cancellation
        int16_t mono_raw = stereo_buffer[i * 2];

        // 1. First-Order IIR DC-Blocking Filter (~60 Hz cutoff @ 16 kHz: alpha = 1000/1024)
        // Strips DC bias and sub-audible mechanical rumble while preserving 100% of human speech fundamentals (85-3000 Hz)
        int32_t diff = (int32_t)mono_raw - (int32_t)prev_raw;
        int32_t filtered = (1000 * (prev_filtered + diff)) >> 10;
        prev_raw = mono_raw;
        prev_filtered = filtered;

        int16_t clean_sample = (int16_t)constrain(filtered, -32768, 32767);
        mono_buffer[i] = clean_sample;

        // 2. Track clean Peak-to-Peak on filtered audio
        if (clean_sample < min_sample) min_sample = clean_sample;
        if (clean_sample > max_sample) max_sample = clean_sample;
      }

      // Store filtered rumble-free audio directly into PSRAM buffer
      size_t chunk_bytes = num_frames * 2;
      if (44 + mono_bytes_written + chunk_bytes <= MAX_PSRAM_REC_SIZE) {
        memcpy(psram_rec_buf + 44 + mono_bytes_written, mono_buffer, chunk_bytes);
        mono_bytes_written += chunk_bytes;
      } else {
        Serial.println("[WARN] PSRAM audio recording buffer limit reached!");
        break;
      }

      stereo_bytes_read += bytes_read;

      uint32_t peak_to_peak = (uint32_t)(max_sample - min_sample);
      uint32_t now = millis();
      chunks_analyzed++;

      // 3. Adaptive Baseline Calibration (first ~160ms)
      if (chunks_analyzed <= 5) {
        noise_floor = (noise_floor + peak_to_peak) / 2;
      } else if (!speech_detected) {
        // Slow background tracking while waiting for Haru to speak
        noise_floor = (noise_floor * 15 + peak_to_peak) >> 4;
      }

      // 4. Trigger Threshold: Noise Floor + Dynamic Headroom
      uint32_t dynamic_threshold = max((uint32_t)1800, noise_floor * 2 + 600);

      if (peak_to_peak > dynamic_threshold) {
        if (!speech_detected) {
          speech_detected = true;
          speech_start_time = now;
          Serial.printf("[VAD] Speech started! (P-P: %u, Baseline: %u, Trigger: %u)\n",
                        peak_to_peak, noise_floor, dynamic_threshold);
        }
        last_speech_time = now;
      }

      // 5. Silence Auto-Truncation
      if (speech_detected) {
        if ((now - last_speech_time >= SILENCE_TIMEOUT_MS) && (now - record_start_time >= MIN_RECORD_MS)) {
          Serial.printf("[VAD] Speech ended cleanly (%u ms silence). Saved %u ms!\n",
                        (now - last_speech_time),
                        (MAX_TOTAL_RECORD_MS > (now - record_start_time)) ? (MAX_TOTAL_RECORD_MS - (now - record_start_time)) : 0);
          break;
        }
      } else {
        if (now - record_start_time >= MAX_WAIT_SPEECH_MS) {
          Serial.println("[VAD] No speech detected within 3.5s. Stopping recording.");
          break;
        }
      }

      if (now - record_start_time >= MAX_TOTAL_RECORD_MS) {
        Serial.println("[VAD] Maximum recording duration reached.");
        break;
      }
    }
    server.handleClient();   // Keep web server responsive during recording
    update_servo_gestures(); // Keep animatronics alive during recording
  }

  free(stereo_buffer);
  free(mono_buffer);

  // Write 44-byte WAV header in memory at start of psram_rec_buf
  write_wav_header_mem(psram_rec_buf, RECORD_SAMPLE_RATE, RECORD_BITS_PER_SAMPLE, 1, mono_bytes_written);
  psram_rec_len = 44 + mono_bytes_written;

  i2s.end();
  is_recording = false;
  uint32_t elapsed_ms = millis() - record_start_time;
  Serial.printf("[RECORD] Finished in %u ms! %u bytes in PSRAM (16kHz Mono, HPF-filtered)\n", elapsed_ms, psram_rec_len);
  return speech_detected;
}

// =========================================================================
// DOWNLOAD RAM'S SPEECH MP3 FROM RENDER DIRECTLY TO SD CARD
// =========================================================================
bool download_ram_speech(const char *local_path) {
  current_state = "thinking";
  Serial.printf("[CLOUD] Downloading Ram's speech MP3 to %s...\n", local_path);

  String url = String(backend_base_url) + "/ram_speech.mp3";

  for (int attempt = 1; attempt <= 4; attempt++) {
    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(30000);
    client.setTimeout(25000);

    HTTPClient http;
    http.begin(client, url);
    http.setTimeout(25000);

    int httpCode = http.GET();

    if (httpCode == HTTP_CODE_OK) {
      int len = http.getSize();
      Serial.printf("[CLOUD] MP3 stream open (%d bytes expected)...\n", len);

      if (SD_MMC.exists(local_path)) {
        SD_MMC.remove(local_path);
      }

      File f = SD_MMC.open(local_path, FILE_WRITE);
      if (!f) {
        Serial.printf("[ERROR] Failed to open %s for writing!\n", local_path);
        http.end();
        client.stop();
        return false;
      }

      uint32_t dl_start = millis();
      int written = http.writeToStream(&f);
      f.close();
      http.end();
      client.stop();

      Serial.printf("[CLOUD] Download complete: %d bytes stored to SD card in %u ms.\n", written, millis() - dl_start);
      return (written > 0);
    } else if (httpCode == 404 && attempt < 4) {
      Serial.printf("[CLOUD] Speech audio synthesizing (404), retrying in 2s (attempt %d/4)...\n", attempt);
      http.end();
      client.stop();
      uint32_t retry_start = millis();
      while (millis() - retry_start < 2000) {
        server.handleClient();
        update_servo_gestures();
        delay(20);
      }
    } else {
      char err_buf[128] = {0};
      client.lastError(err_buf, sizeof(err_buf));
      Serial.printf("[ERROR] /ram_speech.mp3 download failed: %d (TLS: %s | DRAM: %u B, PSRAM: %u B)\n",
                    httpCode, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
      http.end();
      client.stop();
      if (attempt < 4) {
        uint32_t retry_start = millis();
        while (millis() - retry_start < 2000) {
          server.handleClient();
          update_servo_gestures();
          delay(20);
        }
      } else {
        return false;
      }
    }
  }
  return false;
}

// =========================================================================
// PLAY LOCAL MP3 THROUGH RAM'S SPEAKER (UNBLOCKED ANIMATRONIC PLAYBACK)
// =========================================================================
void play_saved_speech(const char *local_path) {
  if (!SD_MMC.exists(local_path)) {
    Serial.printf("[ERROR] Audio file not found: %s\n", local_path);
    current_state = "idle";
    return;
  }

  is_playing = true;
  current_state = "speaking";

  Serial.printf("[Audio] Playing Ram's voice from SD: %s\n", local_path);

  // Hook debug callbacks so any decode status is visible in Serial
  Audio::audio_info_callback = [](Audio::msg_t m) {
    if (m.msg && strlen(m.msg) > 0) {
      Serial.printf("[AUDIO] %s: %s\n", m.s ? m.s : "INFO", m.msg);
    }
  };

  // Dynamically instantiate Audio to freshly acquire I2S TX channel without collision
  active_player = new Audio();
  if (!active_player) {
    Serial.println("[ERROR] Failed to allocate Audio player!");
    is_playing = false;
    current_state = "idle";
    return;
  }

  active_player->setPinout(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN);
  active_player->setVolume(map(current_volume, 0, 100, 0, 21));

  // Re-assert ES8311 DAC gain and speaker PA
  if (es8311_handle) {
    es8311_voice_volume_set(es8311_handle, current_volume, NULL);
  }
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);

  bool started = active_player->connecttoFS(SD_MMC, local_path);
  if (!started) {
    Serial.println("[ERROR] connecttoFS failed!");
    delete active_player;
    active_player = nullptr;
    is_playing = false;
    current_state = "idle";
    return;
  }

  uint32_t last_ui_tick = 0;
  while (active_player && active_player->isRunning()) {
    active_player->loop();

    // UNBLOCKED ANIMATRONICS: Eases servos, nods, and tilts head while Ram speaks!
    update_servo_gestures();

    // Non-blocking UI tick every 40ms: keeps phone web app alive without starving DMA
    if (millis() - last_ui_tick > 40) {
      server.handleClient();
      last_ui_tick = millis();
    }
  }

  // Allow hardware I2S DMA FIFO ringbuffer to completely drain the final syllables (e.g. "ハル")
  // while keeping servo easing smooth and web server responsive
  uint32_t drain_start = millis();
  while (millis() - drain_start < 600) {
    update_servo_gestures();
    server.handleClient();
    delay(10);
  }

  if (active_player) {
    active_player->stopSong();
    delete active_player; // Cleanly disable & delete I2S channel for future mic recording
    active_player = nullptr;
  }

  is_playing = false;
  current_state = "idle";
  Serial.println("[Audio] Playback finished.\n");
  Serial.println("Press 'r' anytime to speak to Ram again!");
}

// =========================================================================
// STREAM RAM'S SPEECH DIRECTLY OVER WI-FI (ZERO SD CARD DEPENDENCY FALLBACK)
// =========================================================================
void play_cloud_speech(const char *url) {
  is_playing = true;
  current_state = "speaking";

  Serial.printf("[Audio] Streaming Ram's voice directly from cloud: %s\n", url);

  for (int attempt = 1; attempt <= 4; attempt++) {
    active_player = new Audio();
    if (!active_player) {
      Serial.println("[ERROR] Failed to allocate Audio player!");
      is_playing = false;
      current_state = "idle";
      return;
    }

    active_player->setPinout(I2S_BCK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN);
    active_player->setVolume(map(current_volume, 0, 100, 0, 21));

    if (es8311_handle) {
      es8311_voice_volume_set(es8311_handle, current_volume, NULL);
    }
    IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);

    bool started = active_player->connecttohost(url);
    if (!started) {
      delete active_player;
      active_player = nullptr;
      if (attempt < 4) {
        Serial.printf("[CLOUD] Speech pending (still synthesizing?), retrying in 2s (attempt %d/4)...\n", attempt);
        uint32_t retry_start = millis();
        while (millis() - retry_start < 2000) {
          server.handleClient();
          update_servo_gestures();
          delay(20);
        }
        continue;
      } else {
        Serial.println("[ERROR] connecttohost failed after 4 attempts!");
        is_playing = false;
        current_state = "idle";
        return;
      }
    }

    uint32_t last_ui_tick = 0;
    while (active_player && active_player->isRunning()) {
      active_player->loop();
      update_servo_gestures();
      if (millis() - last_ui_tick > 40) {
        server.handleClient();
        last_ui_tick = millis();
      }
    }

    uint32_t drain_start = millis();
    while (millis() - drain_start < 500) {
      update_servo_gestures();
      server.handleClient();
      delay(10);
    }

    if (active_player) {
      active_player->stopSong();
      delete active_player;
      active_player = nullptr;
    }

    is_playing = false;
    current_state = "idle";
    Serial.println("[Audio] Playback finished.\n");
    Serial.println("Press 'r' anytime to speak to Ram again!");
    return;
  }
}

// =========================================================================
// HELPER: DOWNLOAD AND PLAY RAM'S SPEECH
// =========================================================================
void fetch_and_play_ram_speech() {
  const char *local_path = "/ram_reply.mp3";
  if (sd_card_available && download_ram_speech(local_path)) {
    play_saved_speech(local_path);
  } else {
    Serial.println("[WARN] SD cache unavailable. Streaming Ram's speech directly from cloud over Wi-Fi...");
    String stream_url = String(backend_base_url) + "/ram_speech.mp3";
    play_cloud_speech(stream_url.c_str());
  }
}

// =========================================================================
// UNIFIED PARSER & HANDLER FOR BACKEND RESPONSES
// =========================================================================
bool apply_backend_reply(const String &response) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, response);
  if (err) {
    Serial.printf("[JSON ERROR] Failed to parse backend response: %s\n", err.c_str());
    current_state = "idle";
    return false;
  }

  int s_val = doc["seq"] | -1;
  if (s_val > 0) last_played_seq = s_val;

  if (doc["transcript"].is<const char*>()) {
    const char* t = doc["transcript"];
    if (t && strlen(t) > 0) {
      last_transcript = String(t);
    } else {
      last_transcript = "";
    }
  }

  if (doc["identity"].is<const char*>()) {
    last_identity = doc["identity"].as<String>();
  }
  if (doc["similarity"].is<float>()) {
    last_similarity = doc["similarity"].as<float>();
  }

  // If transcript is empty or generic, set accurate contextual description based on identity
  if (last_transcript.length() == 0 || last_transcript == "..." || last_transcript.indexOf("Haru showed") >= 0 || last_transcript.indexOf("Unknown person") >= 0) {
    if (last_identity == "STRANGER") {
      last_transcript = "(Unknown person appeared at Haru's desk)";
    } else {
      last_transcript = "(Haru showed himself to Ram)";
    }
  }

  const char* reply_str = nullptr;
  if (doc["reply"].is<const char*>()) {
    reply_str = doc["reply"];
  } else if (doc["text"].is<const char*>()) {
    reply_str = doc["text"];
  }

  if (reply_str && strlen(reply_str) > 0) {
    last_reply = String(reply_str);
  }

  const char* motion_str = nullptr;
  if (doc["motion"].is<const char*>()) {
    motion_str = doc["motion"];
  }
  if (motion_str && strlen(motion_str) > 0) {
    trigger_motion(String(motion_str));
  }

  if (last_identity == "STRANGER") {
    set_mood(ram_mood - 20);
    Serial.printf("[ALERT] 🚨 INTRUDER / STRANGER DETECTED (Similarity: %.2f)! Ram mood dropped to %d%%\n", last_similarity, ram_mood);
  } else if (last_identity == "HARU") {
    set_mood(ram_mood + 8);
    Serial.printf("[AUTH] 🌸 Haru-sama identified (Similarity: %.2f)! Ram mood boosted to %d%%\n", last_similarity, ram_mood);
  }

  current_state = "speaking";
  // Give phone Web App an immediate window to fetch /api/status so subtitles show before/as audio starts
  uint32_t t_sync = millis();
  while (millis() - t_sync < 100) {
    server.handleClient();
    delay(5);
  }

  Serial.println("--------------------------------------------------");
  if (last_transcript.length() > 0) {
    if (last_identity == "STRANGER") {
      Serial.printf("[INTRUDER] Stranger: \"%s\"\n", last_transcript.c_str());
    } else {
      Serial.printf("[YOU] Haru: \"%s\"\n", last_transcript.c_str());
    }
  }
  Serial.printf("[RAM] Motion: [%s] | Ram says: \"%s\" (seq=%d)\n",
                motion_str ? motion_str : "NONE", last_reply.c_str(), last_played_seq);
  Serial.println("--------------------------------------------------");

  fetch_and_play_ram_speech();
  return true;
}

// =========================================================================
// RECOVERY HELPER: POLL /status AND PLAY RAM'S SPEECH
// =========================================================================
bool wait_and_play_ram_reply(uint32_t max_wait_ms) {
  Serial.println("[CLOUD] Upload completed. Waiting for Ram's voice to finish generating...");
  uint32_t poll_start = millis();
  while (millis() - poll_start < max_wait_ms) {
    uint32_t retry_start = millis();
    while (millis() - retry_start < 2000) {
      server.handleClient();   // Keep phone UI responsive
      update_servo_gestures(); // Keep animatronics alive
      delay(20);
    }

    WiFiClientSecure poll_client;
    poll_client.setInsecure();
    poll_client.setHandshakeTimeout(30000);
    poll_client.setTimeout(15000);
    HTTPClient poll_http;
    poll_http.begin(poll_client, String(backend_base_url) + "/status");
    poll_http.setTimeout(15000);
    if (strlen(chat_api_token) > 0) {
      poll_http.addHeader("X-Chibi-Ram-Token", chat_api_token);
    }
    int code = poll_http.GET();
    Serial.printf("[POLL] /status check: code=%d\n", code);

    if (code == 200) {
      String resp = poll_http.getString();
      poll_http.end();
      poll_client.stop();

      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, resp);
      if (!err) {
        bool has_new = doc["has_new_audio"] | false;
        int seq_val = doc["seq"] | -1;
        if (has_new && seq_val != last_played_seq && seq_val >= 0) {
          return apply_backend_reply(resp);
        }
      }
    } else {
      char err_buf[128] = {0};
      poll_client.lastError(err_buf, sizeof(err_buf));
      Serial.printf("[POLL ERROR] /status failed: code=%d (TLS: %s | DRAM: %u B)\n",
                    code, err_buf[0] ? err_buf : "None", ESP.getFreeHeap());
      poll_http.end();
      poll_client.stop();
    }
  }
  Serial.println("[ERROR] Timed out waiting for Ram's reply.");
  current_state = "idle";
  return false;
}

// =========================================================================
// UPLOAD MULTIMODAL (CAMERA JPEG + PSRAM AUDIO) TO RENDER /vision-chat
// =========================================================================
void upload_vision_and_chat() {
  if (!psram_rec_buf || psram_rec_len < 44) {
    Serial.println("[ERROR] No recorded audio in PSRAM for vision-chat!");
    current_state = "idle";
    return;
  }

  current_state = "thinking";

  // Flush any stale frame buffered from previous idle state (single fb_count)
  camera_fb_t *stale_fb = esp_camera_fb_get();
  if (stale_fb) {
    esp_camera_fb_return(stale_fb);
  }

  JpegFrame frame = get_camera_jpeg(80);
  if (!frame.buf || frame.len == 0) {
    Serial.println("[WARN] Could not capture camera JPEG. Falling back to voice-chat.");
    release_camera_jpeg(frame);
    upload_voice_and_chat();
    return;
  }

  String boundary   = "----RamBoundary" + String(millis());
  String head_image = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String head_audio = "\r\n--" + boundary + "\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"voice.wav\"\r\nContent-Type: audio/wav\r\n\r\n";
  String tail       = "\r\n--" + boundary + "--\r\n";

  size_t total_size = head_image.length() + frame.len + head_audio.length() + psram_rec_len + tail.length();

  // Assemble directly in PSRAM to eliminate SD flash I/O (~400ms saved & zero flash wear)
  uint8_t *payload_buf = (uint8_t *)ps_malloc(total_size);
  if (!payload_buf) {
    payload_buf = (uint8_t *)malloc(total_size);
  }

  if (!payload_buf) {
    Serial.println("[ERROR] Failed to allocate PSRAM for multipart upload!");
    release_camera_jpeg(frame);
    current_state = "idle";
    return;
  }

  size_t offset = 0;
  memcpy(payload_buf + offset, head_image.c_str(), head_image.length());
  offset += head_image.length();

  memcpy(payload_buf + offset, frame.buf, frame.len);
  offset += frame.len;
  release_camera_jpeg(frame); // Frame copied into PSRAM, release camera buffer immediately!

  memcpy(payload_buf + offset, head_audio.c_str(), head_audio.length());
  offset += head_audio.length();

  // Stream WAV directly from PSRAM buffer (zero SD read stalls)
  memcpy(payload_buf + offset, psram_rec_buf, psram_rec_len);
  offset += psram_rec_len;

  memcpy(payload_buf + offset, tail.c_str(), tail.length());
  offset += tail.length();

  Serial.printf("[CLOUD] Uploading %u bytes of multipart data from PSRAM to /vision-chat...\n", total_size);

  String endpoint = String(backend_base_url) + "/vision-chat";

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(65000);

  HTTPClient http;
  http.begin(client, endpoint);
  http.setTimeout(65000); // 65s timeout allows Gemini Vision inference
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }
  http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

  int httpCode = http.sendRequest("POST", payload_buf, total_size);
  free(payload_buf);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    apply_backend_reply(response);
  } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
    http.end();
    client.stop();
    wait_and_play_ram_reply(60000);
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ERROR] /vision-chat failed: %d (TLS: %s | DRAM free: %u B, PSRAM: %u B)\n",
                  httpCode, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
    http.end();
    client.stop();
    current_state = "idle";
  }
}

// =========================================================================
// UPLOAD SNAPSHOT FOR ONE-TAP PHOTO ROAST (/snap-roast)
// =========================================================================
void upload_snap_roast() {
  if (!camera_available) {
    Serial.println("[CAMERA] Camera not available for snap roast.");
    return;
  }

  current_state = "thinking";

  // Flush any stale frame buffered from previous idle state
  camera_fb_t *stale_fb = esp_camera_fb_get();
  if (stale_fb) {
    esp_camera_fb_return(stale_fb);
  }

  JpegFrame frame = get_camera_jpeg(80);
  if (!frame.buf || frame.len == 0) {
    Serial.println("[CAMERA ERROR] Failed to grab JPEG frame for snap roast!");
    release_camera_jpeg(frame);
    current_state = "idle";
    return;
  }

  Serial.printf("[CLOUD] Snapping photo (%u bytes JPEG) & sending to Render /snap-roast...\n", frame.len);

  String endpoint = String(backend_base_url) + "/snap-roast";

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(65000);

  HTTPClient http;
  http.begin(client, endpoint);
  http.setTimeout(65000);
  http.addHeader("Content-Type", "image/jpeg");
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }

  int httpCode = http.sendRequest("POST", frame.buf, frame.len);
  release_camera_jpeg(frame);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    last_transcript = "";
    apply_backend_reply(response);
  } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
    http.end();
    client.stop();
    wait_and_play_ram_reply(60000);
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ERROR] /snap-roast failed: %d (TLS: %s | DRAM free: %u B, PSRAM: %u B)\n",
                  httpCode, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
    http.end();
    client.stop();
    current_state = "idle";
  }
}

// =========================================================================
// UPLOAD WAV AUDIO FROM PSRAM DIRECTLY TO /voice-chat (ZERO SD WEAR)
// =========================================================================
void upload_voice_and_chat() {
  if (!psram_rec_buf || psram_rec_len < 44) {
    Serial.println("[ERROR] No recorded audio in PSRAM to upload!");
    current_state = "idle";
    return;
  }

  current_state = "thinking";
  Serial.printf("\n[CLOUD] Uploading %u bytes of audio from PSRAM directly to /voice-chat...\n", psram_rec_len);

  String endpoint = String(backend_base_url) + "/voice-chat";

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(65000);

  HTTPClient http;
  http.begin(client, endpoint);
  http.setTimeout(65000);
  http.addHeader("Content-Type", "audio/wav");
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }

  int httpCode = http.sendRequest("POST", psram_rec_buf, psram_rec_len);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    apply_backend_reply(response);
  } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
    http.end();
    client.stop();
    wait_and_play_ram_reply(60000);
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ERROR] /voice-chat failed: %d (TLS: %s | DRAM free: %u B, PSRAM: %u B)\n",
                  httpCode, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
    http.end();
    client.stop();
    current_state = "idle";
  }
}

// =========================================================================
// SEND DIRECT TEXT PROMPT (/chat)
// =========================================================================
void send_text_chat(const String &msg) {
  current_state = "thinking";
  last_transcript = msg;

  String endpoint = String(backend_base_url) + "/chat";

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(65000);

  HTTPClient http;
  http.begin(client, endpoint);
  http.setTimeout(65000);
  http.addHeader("Content-Type", "application/json");
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }

  String payload = "{\"message\":\"" + escapeJson(msg) + "\"}";
  int httpCode = http.sendRequest("POST", payload);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    apply_backend_reply(response);
  } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
    http.end();
    client.stop();
    wait_and_play_ram_reply(60000);
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ERROR] /chat failed: %d (TLS: %s | DRAM free: %u B, PSRAM: %u B)\n",
                  httpCode, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
    http.end();
    client.stop();
    current_state = "idle";
  }
}

// =========================================================================
// ENROLL HARU'S FACE: AUTOMATIC LOAD FROM SD CARD OR CAMERA
// =========================================================================
bool check_and_enroll_sd_haru() {
  if (!sd_card_available) {
    Serial.println("[ENROLL] SD card not mounted, cannot check for haru.jpg.");
    return false;
  }

  const char *candidate_paths[] = {
    "/haru.jpg", "/haru.jpeg", "/haru.png", "/haru.webp", "/haru.bmp",
    "/Haru.jpg", "/Haru.jpeg", "/Haru.png", "/Haru.webp", "/Haru.bmp",
    "/HARU.JPG", "/HARU.JPEG", "/HARU.PNG", "/HARU.WEBP", "/HARU.BMP",
    "/haru",     "/Haru",      "/HARU"
  };

  const char *found_path = nullptr;
  for (size_t i = 0; i < sizeof(candidate_paths) / sizeof(candidate_paths[0]); i++) {
    if (SD_MMC.exists(candidate_paths[i])) {
      found_path = candidate_paths[i];
      break;
    }
  }

  // Fallback: scan root directory if none of the standard names matched exactly
  static char fallback_path[64];
  if (!found_path) {
    File root = SD_MMC.open("/");
    if (root && root.isDirectory()) {
      File file = root.openNextFile();
      while (file) {
        if (!file.isDirectory()) {
          const char *fname = file.name();
          const char *base = (fname[0] == '/') ? fname + 1 : fname;
          if (strncasecmp(base, "haru", 4) == 0) {
            snprintf(fallback_path, sizeof(fallback_path), "/%s", base);
            found_path = fallback_path;
            file.close();
            break;
          }
        }
        file.close();
        file = root.openNextFile();
      }
      root.close();
    }
  }

  if (!found_path) {
    Serial.println("[ENROLL] No haru image (/haru.jpg) found on SD card.");
    return false;
  }

  File f = SD_MMC.open(found_path, FILE_READ);
  if (!f) {
    Serial.printf("[ENROLL ERROR] Failed to open %s on SD card!\n", found_path);
    return false;
  }

  size_t file_size = f.size();
  Serial.printf("[ENROLL] Found %s on SD card (%u bytes)! Uploading to /enroll...\n", found_path, file_size);

  uint8_t *img_buf = (uint8_t *)ps_malloc(file_size);
  if (!img_buf) {
    img_buf = (uint8_t *)malloc(file_size);
  }
  if (!img_buf) {
    Serial.println("[ENROLL ERROR] Failed to allocate memory for image buffer!");
    f.close();
    return false;
  }

  size_t bytes_read = f.read(img_buf, file_size);
  f.close();

  if (bytes_read != file_size) {
    Serial.println("[ENROLL ERROR] Incomplete file read from SD card!");
    free(img_buf);
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(45000);

  HTTPClient http;
  http.begin(client, String(backend_base_url) + "/enroll");
  http.setTimeout(45000);
  http.addHeader("Content-Type", "image/jpeg");
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }

  int httpCode = http.sendRequest("POST", img_buf, file_size);
  free(img_buf);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    Serial.printf("[ENROLL SUCCESS] Haru-sama enrolled with backend: %s\n", response.c_str());
    last_identity = "HARU";
    last_similarity = 1.0f;
    trigger_motion("BOW");
    return true;
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ENROLL FAILED] /enroll returned code %d (TLS: %s)\n", httpCode, err_buf[0] ? err_buf : "None");
    http.end();
    client.stop();
    return false;
  }
}

bool enroll_camera_haru() {
  if (!camera_available) {
    Serial.println("[ENROLL ERROR] Camera offline, cannot capture enrollment photo!");
    return false;
  }

  Serial.println("[ENROLL] Please face the camera with good lighting (capturing in 1 second)...");
  trigger_motion("TILT");
  delay(1000);

  JpegFrame frame = get_camera_jpeg(85);
  if (!frame.buf || frame.len == 0) {
    Serial.println("[ENROLL ERROR] Failed to grab frame for enrollment!");
    release_camera_jpeg(frame);
    return false;
  }

  Serial.printf("[ENROLL] Uploading photo (%u bytes) to /enroll...\n", frame.len);

  WiFiClientSecure client;
  client.setInsecure();
  client.setHandshakeTimeout(30000);
  client.setTimeout(45000);

  HTTPClient http;
  http.begin(client, String(backend_base_url) + "/enroll");
  http.setTimeout(45000);
  http.addHeader("Content-Type", "image/jpeg");
  if (strlen(chat_api_token) > 0) {
    http.addHeader("X-Chibi-Ram-Token", chat_api_token);
  }

  int httpCode = http.sendRequest("POST", frame.buf, frame.len);
  release_camera_jpeg(frame);

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    http.end();
    client.stop();
    Serial.printf("[ENROLL SUCCESS] Haru-sama enrolled from camera: %s\n", response.c_str());
    last_identity = "HARU";
    last_similarity = 1.0f;
    trigger_motion("BOW");
    return true;
  } else {
    char err_buf[128] = {0};
    client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[ENROLL FAILED] /enroll returned code %d (TLS: %s)\n", httpCode, err_buf[0] ? err_buf : "None");
    http.end();
    client.stop();
    return false;
  }
}

// =========================================================================
// 🌗 ENVIRONMENTAL AWARENESS (CAMERA AS AMBIENT LIGHT SENSOR)
// =========================================================================
void check_ambient_light() {
  if (!camera_available || current_state != "idle" || is_recording || is_playing) return;

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  if (fb->format != PIXFORMAT_RGB565 || fb->len < 2) {
    esp_camera_fb_return(fb);
    return;
  }

  uint32_t total_brightness = 0;
  uint32_t pixel_count = 0;

  // Sample every 10th pixel (20 bytes step) safely
  for (size_t i = 0; i + 1 < fb->len; i += 20) {
    uint16_t pixel = fb->buf[i] | (fb->buf[i + 1] << 8);
    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;
    uint32_t lum = (r * 8 * 299 + g * 4 * 587 + b * 8 * 114) / 1000;
    total_brightness += lum;
    pixel_count++;
  }
  esp_camera_fb_return(fb); // Release frame buffer immediately

  if (pixel_count > 0) {
    last_ambient_lux = (uint8_t)(total_brightness / pixel_count);
    if (last_ambient_lux < 20) {
      Serial.printf("[ENV] 🌑 Room is dark (lux=%d). Ram enters cozy doze pose...\n", last_ambient_lux);
      set_mood(max(10, ram_mood - 2));
      if (active_motion == MOTION_IDLE) {
        trigger_motion("BOW");
      }
    } else if (last_ambient_lux > 180) {
      Serial.printf("[ENV] ☀️ Room is bright (lux=%d)!\n", last_ambient_lux);
    }
  }
}

// =========================================================================
// 🧠 AUTONOMY, BOREDOM & MOOD ENGINE
// =========================================================================
void update_autonomy_and_mood() {
  uint32_t now = millis();

  // 1. Mood Decay (Loses 5 mood every 2 hours of inactivity)
  if (now - last_interaction_time > 7200000) {
    last_interaction_time = now; // ALWAYS reset the 2-hour timer!
    if (ram_mood > 20) {
      set_mood(max(10, ram_mood - 5));
      Serial.printf("[MOOD] Ram feels ignored. Mood dropped to %d%%\n", ram_mood);
    }
  }

  // 2. Boredom Behaviors (Only when completely idle)
  if (current_state == "idle" && active_motion == MOTION_IDLE && !is_recording && !is_playing) {
    uint32_t idle_time = now - last_interaction_time;

    if (idle_time > 600000 && now - last_boredom_check_time > 300000) { // 10 minutes (checked FIRST so not shadowed)
      Serial.println("[BOREDOM] Ram is dozing off waiting for Haru-sama...");
      trigger_motion("NOD");
      last_boredom_check_time = now;
    } else if (idle_time > 300000 && now - last_boredom_check_time > 300000) { // 5 minutes
      Serial.println("[BOREDOM] Ram looks around wondering where Haru-sama is...");
      trigger_motion("TILT");
      last_boredom_check_time = now;
    }
  }

  // 3. 8:00 AM Morning Greeting (Once per day)
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 20)) {
    if (timeinfo.tm_hour == 8 && timeinfo.tm_min == 0 && !morning_greeting_sent) {
      morning_greeting_sent = true;
      Serial.println("[ROUTINE] ☀️ 8:00 AM! Triggering Good Morning greeting for Haru-sama.");
      pending_say_message = "おはようございます、ハル様！ よく眠れましたか？";
      set_mood(ram_mood + 15);
    } else if (timeinfo.tm_hour != 8) {
      morning_greeting_sent = false; // Reset for tomorrow
    }
  }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n==================================================");
  Serial.println("   🌸 Chibi Ram - Vision & Voice Companion 🌸");
  Serial.println("==================================================");

  // 0. Restore Ram's Soul (NVS persistent memory)
  ram_soul.begin("ram_soul", false);
  ram_mood = ram_soul.getInt("mood", 65);
  Serial.printf("[SOUL] 💖 Restored Ram's mood from flash memory: %d%%\n", ram_mood);

  // 1. Initialize I2C Bus & IO Expander
  DEV_I2C_Init();
  IO_EXTENSION_Init();

  // Enable peripherals: IO2 (Microphones), IO6 (Camera LDO Power)
  IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);
  IO_EXTENSION_Output(IO_EXTENSION_IO_6, 1);

  // Allow power rails (especially camera LDO on IO6) to fully stabilize
  delay(150);

  // 2. Diagnostics: Check PSRAM detection
  if (psramFound()) {
    Serial.printf("[PSRAM] Detected Octal PSRAM: %u MB free\n", ESP.getFreePsram() / (1024 * 1024));
  } else {
    Serial.println("[PSRAM WARNING] PSRAM is NOT detected! Check Arduino IDE: Tools -> PSRAM: 'OPI PSRAM'!");
  }

  // 3. Initialize Camera FIRST (OV3660 in PSRAM, before any large buffers fragment heap)
  camera_available = camera_init();
  if (!camera_available) {
    Serial.println("[INFO] Running in Voice, Animatronic & Web Controller mode (Camera offline).");
  }

  // 4. Mount SD Card (Used for fast MP3 cache playback)
  if (!sdcard_init()) {
    Serial.println("[WARN] SD Card unavailable. System will stream audio directly over Wi-Fi (Zero-SD mode).");
  }

  // 5. Enable onboard Speaker Power Amplifier via IO4 (after SD init per hardware spec)
  IO_EXTENSION_Output(IO_EXTENSION_IO_4, 1);

  // 6. Initialize PCA9685 Servo Driver at 0x41 (Zero dependency, solder A0 bridged)
  pca9685_init();

  // 7. Initialize ES7210 Microphone ADC at 0x40
  if (es7210_mic_init() != ESP_OK) {
    Serial.println("[FATAL] ES7210 initialization failed!");
  }

  // 8. Initialize ES8311 Speaker DAC at 0x18
  if (es8311_speaker_init() != ESP_OK) {
    Serial.println("[FATAL] ES8311 initialization failed!");
  }

  // 9. Allocate Audio Recording Buffer (Prefer PSRAM, fall back to heap if needed)
  if (psramFound()) {
    psram_rec_buf = (uint8_t *)ps_malloc(MAX_PSRAM_REC_SIZE);
  }
  if (!psram_rec_buf) {
    psram_rec_buf = (uint8_t *)malloc(MAX_PSRAM_REC_SIZE);
  }
  if (psram_rec_buf) {
    Serial.printf("[AUDIO BUF] Recording buffer ready (%u KB)!\n", MAX_PSRAM_REC_SIZE / 1024);
  } else {
    Serial.println("[FATAL] Failed to allocate audio recording buffer!");
  }

  // 10. Connect to Wi-Fi & start mDNS
  connect_to_wifi();
  delay(500); // Allow Wi-Fi IP stack and DNS resolver to fully settle

  // 11. Start Embedded Web Controller
  setup_web_server();

  // 12. Wake up Render backend on boot
  Serial.println("[CLOUD] Waking up Render backend (waiting for container spin-up if cold)...");
  WiFiClientSecure warmup_client;
  warmup_client.setInsecure();
  warmup_client.setHandshakeTimeout(30000);
  warmup_client.setTimeout(50000);
  HTTPClient warmup_http;
  warmup_http.begin(warmup_client, String(backend_base_url) + "/health");
  warmup_http.setTimeout(50000); // 50s allows Render free container to fully spin up
  int warmup_code = warmup_http.GET();

  if (warmup_code == 200) {
    Serial.println("[CLOUD] Backend is AWAKE & ready!");
  } else {
    char err_buf[128] = {0};
    warmup_client.lastError(err_buf, sizeof(err_buf));
    Serial.printf("[CLOUD] Warmup ping returned code %d (TLS: %s | DRAM free: %u B, PSRAM: %u B). Continuing.\n",
                  warmup_code, err_buf[0] ? err_buf : "None", ESP.getFreeHeap(), ESP.getFreePsram());
  }
  warmup_http.end();
  warmup_client.stop();

  // 13. Auto-Enroll Haru-sama from SD card (/haru.jpg)
  if (sd_card_available) {
    Serial.println("[ENROLL] Checking SD card for Haru-sama's enrollment photo (/haru.jpg)...");
    check_and_enroll_sd_haru();
  }

  Serial.println("\n==================================================");
  Serial.println("   🌸 CHIBI RAM IS READY TO CONVERSE! 🌸");
  Serial.printf("   📱 Web Controller: http://%s\n", WiFi.localIP().toString().c_str());
  Serial.println("   📡 Local Hostname: http://chibi-ram.local");
  Serial.printf("   📷 Vision Camera:  %s\n", camera_available ? "ONLINE" : "STANDBY (Not Detected)");
  Serial.printf("   🤖 PCA9685 Servos: %s\n", pca9685_available ? "ONLINE (0x41)" : "OFFLINE (A0 bridged?)");
  Serial.printf("   👤 Master Auth:    %s\n", last_identity.c_str());
  Serial.println("==================================================");
  Serial.println("Commands:");
  Serial.println("  'v' -> Look & React (captures camera + records voice)");
  Serial.println("  'r' -> Speak to Ram (voice only)");
  Serial.println("  's' -> Snap & Roast (camera only)");
  Serial.println("  'e' -> Enroll Haru-sama's face (from /haru.jpg on SD or camera)");
  Serial.println("  'p' -> Replay Ram's last speech");
  Serial.println("  'b' -> Test Bow gesture (Maid greeting to Haru-sama)");
  Serial.println("  't' -> Test Tilt gesture");
  Serial.println("  'n' -> Test Nod gesture");
  Serial.println("  'h' -> Test Shake gesture");
  Serial.println("  'w' -> Test Wave gesture");
  Serial.println("  'x' -> Factory reset Ram's mood (to 65%)");
  Serial.println("==================================================\n");

  last_interaction_time = millis();
  last_env_check_time = millis();
  last_boredom_check_time = millis();
}

// =========================================================================
// LOOP
// =========================================================================
void loop() {
  server.handleClient();   // Process incoming web app requests continuously
  update_servo_gestures(); // Smooth 50Hz animatronic easing

  // Autonomy, Boredom Behaviors & Mood Engine
  update_autonomy_and_mood();

  // Environmental Light Check (every 5 minutes when idle)
  if (millis() - last_env_check_time > 300000) {
    last_env_check_time = millis();
    check_ambient_light();
  }

  // Proactive Keep-Alive: Ping Render every 10 minutes to reset its 15-min sleep timer
  static uint32_t last_cloud_keepalive = millis();
  if (millis() - last_cloud_keepalive > 600000) { // 600,000 ms = 10 minutes
    last_cloud_keepalive = millis();
    if (!is_recording && !is_playing && current_state == "idle") {
      WiFiClientSecure keepalive_client;
      keepalive_client.setInsecure();
      keepalive_client.setHandshakeTimeout(30000);
      keepalive_client.setTimeout(10000);
      HTTPClient keepalive_http;
      keepalive_http.begin(keepalive_client, String(backend_base_url) + "/health");
      keepalive_http.setTimeout(10000);
      int code = keepalive_http.GET();
      if (code == 200) {
        Serial.printf("[CLOUD] 10-min keep-alive sent to prevent sleep (code=%d)\n", code);
      } else {
        char err_buf[128] = {0};
        keepalive_client.lastError(err_buf, sizeof(err_buf));
        Serial.printf("[CLOUD] Keep-alive code %d (TLS: %s)\n", code, err_buf[0] ? err_buf : "None");
      }
      keepalive_http.end();
      keepalive_client.stop();
    }
  }

  // Handle actions triggered from the Phone Web App
  if (pending_look_and_react) {
    last_interaction_time = millis();
    set_mood(ram_mood + 4);
    pending_look_and_react = false;
    if (record_voice(RECORD_DURATION_SEC)) {
      delay(50);
      if (camera_available) {
        upload_vision_and_chat();
      } else {
        upload_voice_and_chat();
      }
    } else {
      current_state = "idle";
      Serial.println("[RECORD] No speech, skipping upload.");
    }
  } else if (pending_record) {
    last_interaction_time = millis();
    set_mood(ram_mood + 4);
    pending_record = false;
    if (record_voice(RECORD_DURATION_SEC)) {
      delay(50);
      upload_voice_and_chat();
    } else {
      current_state = "idle";
      Serial.println("[RECORD] No speech, skipping upload.");
    }
  } else if (pending_snap_roast) {
    last_interaction_time = millis();
    pending_snap_roast = false;
    if (camera_available) {
      upload_snap_roast();
    } else {
      Serial.println("[CAMERA] Camera not detected. Plug in camera to enable Snap & Roast!");
    }
  } else if (pending_replay) {
    last_interaction_time = millis();
    pending_replay = false;
    if (sd_card_available && SD_MMC.exists("/ram_reply.mp3")) {
      play_saved_speech("/ram_reply.mp3");
    } else {
      fetch_and_play_ram_speech();
    }
  } else if (pending_say_message.length() > 0) {
    last_interaction_time = millis();
    set_mood(ram_mood + 4);
    String msg = pending_say_message;
    pending_say_message = "";
    send_text_chat(msg);
  }

  // Handle keyboard commands from Serial Monitor (for testing and direct control)
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') return;
    while (Serial.available()) {
      char c = Serial.peek();
      if (c == '\r' || c == '\n' || c == ' ') Serial.read();
      else break;
    }

    if (cmd == 'v' || cmd == 'V') {
      last_interaction_time = millis();
      set_mood(ram_mood + 4);
      if (record_voice(RECORD_DURATION_SEC)) {
        delay(50);
        if (camera_available) {
          upload_vision_and_chat();
        } else {
          Serial.println("[CAMERA] Camera not detected. Running voice-only conversation.");
          upload_voice_and_chat();
        }
      } else {
        current_state = "idle";
        Serial.println("[RECORD] No speech, skipping upload.");
      }
    } else if (cmd == 'r' || cmd == 'R') {
      last_interaction_time = millis();
      set_mood(ram_mood + 4);
      if (record_voice(RECORD_DURATION_SEC)) {
        delay(50);
        upload_voice_and_chat();
      } else {
        current_state = "idle";
        Serial.println("[RECORD] No speech, skipping upload.");
      }
    } else if (cmd == 's' || cmd == 'S') {
      last_interaction_time = millis();
      set_mood(ram_mood + 2);
      if (camera_available) {
        upload_snap_roast();
      } else {
        Serial.println("[CAMERA] Camera not detected. Plug in camera to enable Snap & Roast!");
      }
    } else if (cmd == 'e' || cmd == 'E') {
      Serial.println("[COMMAND] Enrolling Haru-sama...");
      bool enrolled = check_and_enroll_sd_haru();
      if (!enrolled && camera_available) {
        enroll_camera_haru();
      }
    } else if (cmd == 'p' || cmd == 'P') {
      if (sd_card_available && SD_MMC.exists("/ram_reply.mp3")) {
        play_saved_speech("/ram_reply.mp3");
      } else {
        fetch_and_play_ram_speech();
      }
    } else if (cmd == 'b' || cmd == 'B') {
      trigger_motion("BOW");
    } else if (cmd == 't' || cmd == 'T') {
      trigger_motion("TILT");
    } else if (cmd == 'n' || cmd == 'N') {
      trigger_motion("NOD");
    } else if (cmd == 'h' || cmd == 'H') {
      trigger_motion("SHAKE");
    } else if (cmd == 'w' || cmd == 'W') {
      trigger_motion("WAVE");
    } else if (cmd == 'x' || cmd == 'X') {
      Serial.println("[SOUL] ♻️ Factory resetting Ram's mood to 65%...");
      set_mood(65);
      Serial.println("[SOUL] Memory reset. Ram feels balanced again.");
    }
  }
}
