/*
  SOLAR AI SCARECROW - FULL VERSION (v6: FINAL - v4 camera capture + simple alarm)
  (PIR + CAMERA AI + BUZZER + LED + DHT11 + SOIL)
  Wiring:
    PIR     VCC -> 5V     OUT -> IO13   GND -> GND
    Buzzer  VCC -> 3V3    I/O -> IO14   GND -> GND
    LED     IO12 -> resistor -> LED long leg (+), LED short leg (-) -> GND
    Soil    VCC -> 3V3    DO  -> IO15   GND -> GND (AO not connected)
    DHT     VCC -> 3V3    DATA -> IO2   GND -> GND  (DHT11 or DHT22, auto-detected)
*/

// Edge Impulse library exported from your own project (Arduino library .zip).
// If your project has a different name, change this include to match.
#include <bird_detector_inferencing.h>

#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_system.h"
#include "img_converters.h"

// ---------- SETTINGS ----------
#define BUZZER_ACTIVE_LOW 1      // 1 = buzzer sounds when pin is LOW (your buzzer)
#define SELF_TEST         1      // 1 = beep + LED test at startup
#define USE_PIR_GATE      1      // 1 = AI checks only after PIR sees motion, 0 = AI checks all the time
#define USE_DHT           1      // 0 = DHT not connected, 1 = connected
#define BIRD_THRESHOLD    0.60   // raise if false alarms, lower if it misses
#define CAMERA_OFF_DURING_ALARM 0  // 1 = switch camera off while buzzer sounds
#define BUZZER_5V_MODE    0      // 0 = buzzer VCC on 3V3 (normal). 1 = buzzer VCC on 5V (louder)
#define BUZZER_PASSIVE    0      // 0 = ACTIVE buzzer (plain ON/OFF), 1 = PASSIVE buzzer (needs a tone)
#define TONE_HZ           2700   // tone pitch used only when BUZZER_PASSIVE = 1
#define POST_CAMERA_TEST  1      // 1 = second beep+LED test AFTER camera starts (finds power problems)
#define CHECKS            3      // how many times AI looks at the camera
#define HITS_NEEDED       1      // 1 = alarm on first bird hit. Use 2 later to reduce false alarms
#define TARGET_LABEL      "bird" // label name used in Edge Impulse

// ---------- PINS ----------
#define PIR_PIN     13
#define BUZZER_PIN  14
#define LED_PIN     12
#define DHT_PIN     2
#define SOIL_PIN    15

#if BUZZER_ACTIVE_LOW
  #define BUZZER_ON   LOW
  #define BUZZER_OFF  HIGH
#else
  #define BUZZER_ON   HIGH
  #define BUZZER_OFF  LOW
#endif

// Buzzer helpers (work for 3V3 and 5V wiring)
void buzzerOn() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
}
void buzzerOff() {
#if BUZZER_5V_MODE
  pinMode(BUZZER_PIN, INPUT);              // let the module's own pull-up switch it fully OFF at 5V
#else
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
#endif
}

#if USE_DHT
  #include "DHT.h"
  DHT dht22(DHT_PIN, DHT22);
  DHT dht11(DHT_PIN, DHT11);
#endif

// ---------- CAMERA PIN MAP (AI-Thinker ESP32-CAM, do not change) ----------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define RAW_W 320
#define RAW_H 240

// ---------- TIMING ----------
const unsigned long PIR_WARMUP_MS        = 30000;
const unsigned long SENSOR_INTERVAL      = 10000;
const unsigned long DETERRENT_DURATION   = 3000;
const unsigned long COOLDOWN_AFTER_ALARM = 5000;

uint8_t *snapshot_buf = nullptr;
bool camera_ok = false;
bool pirReadyShown = false;
unsigned long lastCheck = 0;
unsigned long lastSensorRead = 0;

// ---------- CAMERA SETUP ----------
bool initCamera() {
  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, 0);
  }
  return true;
}

// ---------- IMAGE -> AI INPUT ----------
static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
  size_t pixel_ix = offset * 3;
  size_t pixels_left = length;
  size_t out_ptr_ix = 0;
  while (pixels_left != 0) {
    out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix + 2] << 16) + (snapshot_buf[pixel_ix + 1] << 8) + snapshot_buf[pixel_ix];
    out_ptr_ix++;
    pixel_ix += 3;
    pixels_left--;
  }
  return 0;
}

bool captureForAI(uint32_t img_width, uint32_t img_height) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
  esp_camera_fb_return(fb);
  if (!converted) return false;

  if ((img_width != RAW_W) || (img_height != RAW_H)) {
    ei::image::processing::crop_and_interpolate_rgb888(
      snapshot_buf, RAW_W, RAW_H, snapshot_buf, img_width, img_height);
  }
  return true;
}

// returns bird confidence (0.0 to 1.0), or -1 on error
float getBirdScore() {
  ei::signal_t signal;
  signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
  signal.get_data = &ei_camera_get_data;

  if (!captureForAI((size_t)EI_CLASSIFIER_INPUT_WIDTH, (size_t)EI_CLASSIFIER_INPUT_HEIGHT)) {
    Serial.println("ERROR: camera capture failed");
    return -1;
  }

  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.printf("ERROR: AI model failed (%d)\n", err);
    return -1;
  }

  float score = 0;
#if EI_CLASSIFIER_OBJECT_DETECTION == 1
  // FOMO / object detection model
  for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
    ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
    if (bb.value == 0) continue;
    Serial.printf("   %s: %.2f\n", bb.label, bb.value);
    if (strcasecmp(bb.label, TARGET_LABEL) == 0 && bb.value > score) score = bb.value;
  }
#else
  // Image classification model (bird / no_bird)
  for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.printf("   %s: %.2f\n", result.classification[i].label, result.classification[i].value);
    if (strcasecmp(result.classification[i].label, TARGET_LABEL) == 0) {
      score = result.classification[i].value;
    }
  }
#endif
  return score;
}

// AI looks CHECKS times, needs HITS_NEEDED hits to confirm (reduces false alarms)
bool birdDetected() {
  int hits = 0;
  for (int i = 0; i < CHECKS; i++) {
    float score = getBirdScore();
    Serial.printf("AI check %d: bird score = %.2f\n", i + 1, score);
    if (score >= BIRD_THRESHOLD) hits++;

    if (hits >= HITS_NEEDED) {
      Serial.printf("Bird confirmed (%d hits)\n", hits);
      return true;               // enough proof, stop early
    }
    delay(200);
  }
  Serial.printf("Only %d hit(s), needed %d\n", hits, HITS_NEEDED);
  return false;
}

// ---------- ALARM (buzzer + LED together) ----------
void alarmFor(unsigned long durationMs) {
  unsigned long start = millis();
  unsigned long lastBlink = start;
  bool ledState = true;
  digitalWrite(LED_PIN, HIGH);
#if !BUZZER_PASSIVE
  buzzerOn();          // active buzzer: just switch ON
#endif
  while (millis() - start < durationMs) {
#if BUZZER_PASSIVE
    buzzerOn();        // passive buzzer: make a square wave
    delayMicroseconds(500000UL / TONE_HZ);
    buzzerOff();
    delayMicroseconds(500000UL / TONE_HZ);
#else
    delay(10);
#endif
    if (millis() - lastBlink >= 150) {          // LED blinks every 150 ms
      lastBlink = millis();
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState ? HIGH : LOW);
    }
  }
  buzzerOff();
  digitalWrite(LED_PIN, LOW);
}

void triggerDeterrent() {
  Serial.println(">>> DETERRENT ACTIVATED: Buzzer + LED ON <<<");
#if CAMERA_OFF_DURING_ALARM
  esp_camera_deinit();           // camera OFF -> more power for buzzer + LED
  delay(100);
#endif

  Serial.println("Buzzer + LED ON now...");
  alarmFor(DETERRENT_DURATION);
  Serial.println("Buzzer + LED finished.");

#if CAMERA_OFF_DURING_ALARM
  camera_ok = initCamera();      // camera back ON for the next detection
  Serial.println(camera_ok ? "Camera restarted." : "WARNING: camera restart failed.");
#endif
  Serial.println("Deterrent OFF. Cooling down before next detection...");
  delay(COOLDOWN_AFTER_ALARM);
}

// ---------- DHT (auto-detects DHT22 or DHT11) ----------
#if USE_DHT
bool dhtValid(float t, float h) {
  return !isnan(t) && !isnan(h) && t > -40 && t < 80 && h >= 0 && h <= 100;
}

void readDHT() {
  float t = dht22.readTemperature();
  float h = dht22.readHumidity();
  if (dhtValid(t, h)) {
    Serial.printf("Temperature: %.1f C | Humidity: %.1f %% (DHT22)\n", t, h);
    return;
  }

  delay(1200);
  t = dht11.readTemperature();
  h = dht11.readHumidity();
  if (dhtValid(t, h)) {
    Serial.printf("Temperature: %.1f C | Humidity: %.1f %% (DHT11)\n", t, h);
    return;
  }

  Serial.println("DHT read failed - check VCC on 3V3, DATA on IO2, GND");
}
#endif

// ---------- READ SENSORS ----------
void readSensors() {
  Serial.println("---- Sensor Readings ----");
#if USE_DHT
  readDHT();
#else
  Serial.println("DHT: not connected");
#endif
  int soilRaw = digitalRead(SOIL_PIN);
  Serial.printf("Soil moisture: %s\n", soilRaw == LOW ? "WET" : "DRY");
  Serial.println("-------------------------");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Solar AI Scarecrow FULL - booting up...");
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_BROWNOUT) {
    Serial.println("RESET REASON: BROWNOUT -> POWER PROBLEM (weak supply/cable/battery)");
  } else if (rr == ESP_RST_POWERON) {
    Serial.println("Reset reason: normal power-on");
  } else {
    Serial.printf("Reset reason code: %d\n", (int)rr);
  }
  Serial.printf("Pins -> PIR:%d  BUZZER:%d  LED:%d  SOIL:%d  DHT:%d\n",
                PIR_PIN, BUZZER_PIN, LED_PIN, SOIL_PIN, DHT_PIN);

  buzzerOff();
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(PIR_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);

#if SELF_TEST
  Serial.println("SELF-TEST 1 (before camera): buzzer + LED for 1 second...");
  alarmFor(1000);
  Serial.println("SELF-TEST 1 done.");
#endif

#if USE_DHT
  dht22.begin();
  dht11.begin();
#endif

  camera_ok = initCamera();
  snapshot_buf = (uint8_t *)malloc(RAW_W * RAW_H * 3);
  if (snapshot_buf == nullptr) {
    Serial.println("ERROR: not enough memory for AI image buffer");
    camera_ok = false;
  }

  if (camera_ok) {
    Serial.println("Camera initialized successfully.");
    Serial.printf("AI model ready (input %dx%d).\n", EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT);
  } else {
    Serial.println("WARNING: Camera/AI not ready. Motion-only mode.");
  }
  delay(1000);

#if POST_CAMERA_TEST
  Serial.println("SELF-TEST 2 (after camera): buzzer + LED for 1 second...");
  Serial.println("If this is much quieter/dimmer than TEST 1 = POWER problem.");
  alarmFor(1000);
  Serial.println("SELF-TEST 2 done.");
#endif

  Serial.println("PIR warming up for 30 seconds... please wait.");
}

// ---------- MAIN LOOP ----------
void loop() {
  bool pirWarm = (millis() > PIR_WARMUP_MS);

  if (pirWarm && !pirReadyShown) {
    Serial.println("System ready. Waiting for motion...");
    pirReadyShown = true;
  }

  bool check = false;
#if USE_PIR_GATE
  if (pirWarm && digitalRead(PIR_PIN) == HIGH) {
    Serial.println("PIR triggered - motion detected, checking with AI...");
    check = true;
  }
#else
  if (pirWarm && (millis() - lastCheck > 1000)) {
    lastCheck = millis();
    check = true;
  }
#endif

  if (check) {
    if (!camera_ok) {
      Serial.println("Camera/AI not working - motion-only alarm");
      if (USE_PIR_GATE) triggerDeterrent();
    } else if (birdDetected()) {
      Serial.println("AI RESULT: BIRD/ANIMAL DETECTED");
      triggerDeterrent();
    } else {
      Serial.println("AI RESULT: nothing to scare away");
      delay(1000);
    }
  }

  if (millis() - lastSensorRead >= SENSOR_INTERVAL) {
    readSensors();
    lastSensorRead = millis();
  }
}
