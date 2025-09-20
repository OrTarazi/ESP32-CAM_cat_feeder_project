#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_bt.h"   // to disable BT for power saving (optional)
#include "esp_sleep.h"
#include "driver/rtc_io.h"

/*******************************************************
 * Project: Smart Cat Feeder – ESP32-CAM with PIR & Telegram
 * File:    "cat_feeder.cpp"
 * Author:  Or Tarazi
 * Date:    September 2025
 *
 * Description:
 * Wakes on PIR, snaps a photo, sends it to Telegram with
 * "Feed / Skip" buttons, waits up to 2 minutes for a decision,
 * then dispenses (servo) or goes back to sleep.
 *
 * Notes:
 *  - Fill WIFI_SSID, WIFI_PASSWORD, BOT_TOKEN, CHAT_ID.
 *  - Uses HTTPS with insecure cert (OK for hobby use).
 *  - No external JSON lib; naive string parsing.
 *******************************************************/

// ================== USER CONFIG ==================
const char* WIFI_SSID     = "BeSpotCBF5_2.4";       // <-- change
const char* WIFI_PASSWORD = "8800CBF5";   // <-- change

const char* BOT_TOKEN = "8495637258:AAFyFMY3zb3x4jAgKSwHYjnGaaJ_UnMuAqI";  // <-- change (from BotFather)
const char* CHAT_ID   = "-1003049927695";  

// Optional caption for the photo:
const char* PHOTO_CAPTION = "ESP32-CAM snapshot 📸";

// ============ CAMERA PINS (AI-Thinker) ===========
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

// Onboard flash LED (helpful to see life)
#define LED_FLASH_PIN      4
#define PIR_PIN           13

// ========= POLLING SETTINGS =========
const uint32_t POLL_INTERVAL_MS = 3000;  // poll every ~3s as requested
uint32_t lastPollMs = 0;
// Track Telegram updates so we don't re-handle old ones:
long lastUpdateId = -1;

// ------------ small helpers ------------
void flashBlink(int times = 2, int on_ms = 60, int off_ms = 120) {
  pinMode(LED_FLASH_PIN, OUTPUT);
  for (int i = 0; i < times; ++i) {
    digitalWrite(LED_FLASH_PIN, HIGH);
    delay(on_ms);
    digitalWrite(LED_FLASH_PIN, LOW);
    delay(off_ms);
  }
}

bool ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[WiFi] Reconnecting...");
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint8_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(250);
    Serial.print('.');
    tries++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WiFi] Connected. IP: ");
    Serial.println(WiFi.localIP());
    
    Serial.println("RSSI:");
    Serial.println(WiFi.RSSI());

    return true;
  }
  Serial.println("[WiFi] Failed to connect.");
  return false;
}

void sendPirAlertButtons() {
  telegramSendMessageWithButtons("🚨 Motion detected. What should I do?");
}

bool telegramSendMessageWithButtons(const String& text) {
  WiFiClientSecure client;
  client.setInsecure();

  String host = "api.telegram.org";
  String base = "/bot" + String(BOT_TOKEN) + "/sendMessage";
  // Inline keyboard with two buttons that send callback data
  String replyMarkup =
      "{\"inline_keyboard\":[["
      "{\"text\":\"\\uD83D\\uDCF8 /snap\",\"callback_data\":\"snap\"},"
      "{\"text\":\"\\u274E /ignore\",\"callback_data\":\"ignore\"}"
      "]]}";

  // Build GET with URL-encoded text and reply_markup
  String url = base +
               "?chat_id=" + String(CHAT_ID) +
               "&text=" + urlencode(text) +
               "&reply_markup=" + urlencode(replyMarkup);

  if (!client.connect(host.c_str(), 443)) return false;

  String req =
      "GET " + url + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-CAM\r\n" +
      "Connection: close\r\n\r\n";
  client.print(req);

  uint32_t start = millis();
  while (client.connected() && millis() - start < 6000) {
    while (client.available()) { client.read(); start = millis(); }
  }
  client.stop();
  return true;
}


// Build and send multipart/form-data to Telegram sendPhoto
bool telegramSendPhoto(const uint8_t* jpg_buf, size_t jpg_len, const char* caption) {
  WiFiClientSecure client;
  client.setInsecure(); // quick test: skip CA validation

  String host = "api.telegram.org";
  String url  = "/bot" + String(BOT_TOKEN) + "/sendPhoto";

  String boundary = "----ESP32CamFormBoundary";
  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(CHAT_ID) + "\r\n" +
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"caption\"\r\n\r\n" +
    String(caption ? caption : "") + "\r\n" +
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"snap.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  size_t contentLength = head.length() + jpg_len + tail.length();

  Serial.printf("[TG] Connecting to %s...\n", host.c_str());
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("[TG] Connection failed");
    return false;
  }

  String req =
      "POST " + url + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-CAM\r\n" +
      "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n" +
      "Content-Length: " + String(contentLength) + "\r\n" +
      "Connection: close\r\n\r\n";

  client.print(req);
  client.print(head);

  size_t written = 0;
  while (written < jpg_len) {
    size_t chunk = client.write(jpg_buf + written, jpg_len - written);
    if (chunk == 0) {
      Serial.println("[TG] Write stalled");
      client.stop();
      return false;
    }
    written += chunk;
  }

  client.print(tail);

  Serial.println("[TG] Request sent, waiting for response...");
  uint32_t start = millis();
  while (client.connected() && millis() - start < 8000) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.print(line); // prints headers + JSON
      start = millis();
    }
  }
  client.stop();
  return true;
}

bool telegramSendMessage(const String& text) {
  WiFiClientSecure client;
  client.setInsecure();

  String host = "api.telegram.org";
  String url  = "/bot" + String(BOT_TOKEN) + "/sendMessage"
                "?chat_id=" + String(CHAT_ID) +
                "&text=" + urlencode(text);

  Serial.printf("[TG] Connecting for sendMessage...\n");
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("[TG] Connection failed");
    return false;
  }

  String req =
      "GET " + url + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-CAM\r\n" +
      "Connection: close\r\n\r\n";
  client.print(req);

  uint32_t start = millis();
  while (client.connected() && millis() - start < 6000) {
    while (client.available()) {
      String line = client.readStringUntil('\n');
      // Optional: Serial.print(line);
      start = millis();
    }
  }
  client.stop();
  return true;
}

// Very small urlencode helper (enough for basic text)
String urlencode(const String& s) {
  const char *hex = "0123456789ABCDEF";
  String out;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (('a' <= c && c <= 'z') ||
        ('A' <= c && c <= 'Z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else if (c == ' ') {
      out += "%20";
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

bool initCamera() {
  camera_config_t config;
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
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.jpeg_quality = 30;          // 10=better, 63=smaller
    config.fb_count = 1;
    Serial.println("PSRAM found.");
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 30;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_QVGA);
    s->set_quality(s, 35);
  }
  return true;
}

// Capture & send helper
bool takeAndSendPhoto(const char* caption = PHOTO_CAPTION, uint32_t fresh_wait_ms = 120) {
  Serial.println("[CAM] Capturing (fresh /snap)...");

  // 0) Flush whatever the driver currently has ready
  camera_fb_t* fb = esp_camera_fb_get();
  uint64_t t0_ms = 0;
  if (fb) {
    // Record timestamp of the flushed frame (if available)
    t0_ms = (uint64_t)fb->timestamp.tv_sec * 1000ULL + (fb->timestamp.tv_usec / 1000ULL);
    esp_camera_fb_return(fb);
  }

  // Give the sensor/driver a moment to produce the *next* frame
  delay(fresh_wait_ms);

  // 1) Grab what should now be a *fresh* frame
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Capture failed (no fresh frame)");
    return false;
  }

  // Optional: ensure it's newer than the flushed one; if not, try once more quickly
  uint64_t t1_ms = (uint64_t)fb->timestamp.tv_sec * 1000ULL + (fb->timestamp.tv_usec / 1000ULL);
  if (t1_ms <= t0_ms && t0_ms != 0) {
    // Same-or-older frame edge case—return and try one more time
    esp_camera_fb_return(fb);
    delay(30);
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAM] Capture failed on retry");
      return false;
    }
    t1_ms = (uint64_t)fb->timestamp.tv_sec * 1000ULL + (fb->timestamp.tv_usec / 1000ULL);
  }

  Serial.printf("[CAM] Fresh frame: %u bytes (t=%llu ms)\n", fb->len, (unsigned long long)t1_ms);

  // flashBlink(1, 20, 20);  // optional pre/post light cue
  bool ok = telegramSendPhoto(fb->buf, fb->len, caption);

  // Always return the buffer after use
  esp_camera_fb_return(fb);

  if (ok) {
    Serial.println("[TG] Photo sent.");
    // flashBlink(2, 50, 120);
  } else {
    Serial.println("[TG] Send failed.");
    // flashBlink(8, 30, 60);
  }
  return ok;
}


// Naive parser helpers (no ArduinoJson)
static long extractMaxUpdateId(const String& body) {
  // Find all occurrences of "update_id":<num> and return the maximum
  long maxId = lastUpdateId;
  int idx = 0;
  const String key = "\"update_id\":";
  while (true) {
    idx = body.indexOf(key, idx);
    if (idx < 0) break;
    idx += key.length();
    // skip spaces
    while (idx < (int)body.length() && (body[idx] == ' ')) idx++;
    // read number
    long val = 0;
    bool any = false;
    while (idx < (int)body.length() && isDigit(body[idx])) {
      any = true;
      val = val * 10 + (body[idx] - '0');
      idx++;
    }
    if (any && val > maxId) maxId = val;
  }
  return maxId;
}

static bool bodyContainsSnapForMe(const String& body) {
  // Very simple check: look for a message with "/snap" AND the right chat_id
  // We also accept "/snap@YourBotName" by searching "/snap"
  // To limit to your chat, ensure "\"chat\":{\"id\":CHAT_ID}" also appears near.
  String cidPat = String("\"chat\"") + ":{\"id\":" + String(CHAT_ID);
  int from = 0;
  while (true) {
    int pos = body.indexOf("/snap", from);
    if (pos < 0) break;
    // look back a bit for the matching chat id in the same update block
    int windowStart = max(0, pos - 400);
    int windowEnd   = min((int)body.length(), pos + 400);
    String win = body.substring(windowStart, windowEnd);
    if (win.indexOf(cidPat) >= 0) {
      return true;
    }
    from = pos + 5;
  }
  return false;
}

// Poll Telegram (getUpdates with offset=lastUpdateId+1)
void pollTelegram() {
  if (!ensureWiFi()) return;

  WiFiClientSecure client;
  client.setInsecure();

  String host = "api.telegram.org";
  String url  = "/bot" + String(BOT_TOKEN) + "/getUpdates?timeout=0";
  if (lastUpdateId >= 0) {
    url += "&offset=" + String(lastUpdateId + 1);
  }

  Serial.println("[TG] getUpdates...");
  if (!client.connect(host.c_str(), 443)) {
    Serial.println("[TG] getUpdates connect failed");
    return;
  }

  String req =
      "GET " + url + " HTTP/1.1\r\n" +
      "Host: " + host + "\r\n" +
      "User-Agent: ESP32-CAM\r\n" +
      "Connection: close\r\n\r\n";
  client.print(req);

  // Read the full HTTP response body into a String (naively)
  String response;
  uint32_t start = millis();
  while (client.connected() && millis() - start < 6000) {
    while (client.available()) {
      char c = client.read();
      response += c;
      start = millis();
    }
  }
  client.stop();

  // Separate headers from body
  int bodyIdx = response.indexOf("\r\n\r\n");
  if (bodyIdx < 0) {
    Serial.println("[TG] Bad HTTP response");
    return;
  }
  String body = response.substring(bodyIdx + 4);

  // Debug (optional): Serial.println(body);

  // Update lastUpdateId
  long newMax = extractMaxUpdateId(body);
  if (newMax > lastUpdateId) {
    lastUpdateId = newMax;
    Serial.printf("[TG] lastUpdateId -> %ld\n", lastUpdateId);
  }

  // Handle /snap if present for our chat
  if (bodyContainsSnapForMe(body)) {
    telegramSendMessage("📸 On it! Capturing...");
    takeAndSendPhoto(PHOTO_CAPTION);
  }
}

static void configurePirRtcInput() {
  pinMode(PIR_PIN, INPUT);
  rtc_gpio_deinit((gpio_num_t)PIR_PIN);
  rtc_gpio_init((gpio_num_t)PIR_PIN);
  rtc_gpio_set_direction((gpio_num_t)PIR_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_dis((gpio_num_t)PIR_PIN);
  rtc_gpio_pulldown_en((gpio_num_t)PIR_PIN);  // idle LOW
}

static void logWakeCause() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("[BOOT] Wake: EXT1 (PIR)"); break;
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("[BOOT] Wake: EXT0"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("[BOOT] Wake: TIMER"); break;
    case ESP_SLEEP_WAKEUP_UNDEFINED: Serial.println("[BOOT] Power-on reset"); break;
    default: Serial.printf("[BOOT] Wake cause: %d\n", (int)cause); break;
  }
}

static void enterDeepSleep() {
  configurePirRtcInput();
  esp_sleep_enable_ext1_wakeup(1ULL << PIR_PIN, ESP_EXT1_WAKEUP_ANY_HIGH);
  Serial.println("[SLEEP] Going to deep sleep. PIR HIGH will wake me.");
  delay(50);
  esp_deep_sleep_start();
}

void setup() {
  // Optional: disable Bluetooth stack to save power
  btStop();

  pinMode(LED_FLASH_PIN, OUTPUT);
  digitalWrite(LED_FLASH_PIN, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] ESP32-CAM Telegram /snap bot");
  logWakeCause();

  // Bring up Wi-Fi for messaging & polling
  WiFi.mode(WIFI_STA);
  const bool wifi_ok = ensureWiFi();

  if (!initCamera()) {
    flashBlink(8, 40, 60);
    // Even if camera failed, we still configure sleep so battery isn't drained.
  }

  // Optional hello
  if (wifi_ok) {
    telegramSendMessage("🤖 ESP32-CAM awake. You have 30s; send /snap or /ignore.");
  }

  // ---------- ACTIVE WINDOW AFTER WAKE ----------
  // Stay awake for 30 seconds total, polling every ~3 seconds.
  const uint32_t AWAKE_MS = 30 * 1000UL;
  const uint32_t POLL_PERIOD_MS = 3000UL;
  const uint32_t t_end = millis() + AWAKE_MS;

  while ((int32_t)(t_end - millis()) > 0) {
    if (wifi_ok) {
      pollTelegram();   // <-- your existing single-iteration getUpdates handler
    }
    // Optional: light blink to show alive
    // flashBlink(1, 10, 0);

    // Sleep between polls (keeps CPU idle and saves some power)
    delay(POLL_PERIOD_MS);
  }

  // notify to telegram that going to sleep:

  if (wifi_ok) {
    telegramSendMessage("😴 Going back to deep sleep. Wake me with motion (PIR).");
  }

  // Tear down radios before deep sleep
  
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);

  // Configure EXT1 (PIR HIGH) and sleep
  enterDeepSleep();
}

void loop() {
  // empty
}
