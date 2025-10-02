#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_bt.h"   // to disable BT for power saving (optional)
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "cat_feeder.h"
#include "user_wifi_and_telegram_config.h"

void setup() {
  // Optional: disable Bluetooth stack to save power
  btStop();

  pinMode(LED_FLASH_PIN, OUTPUT);
  digitalWrite(LED_FLASH_PIN, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] ESP32-CAM Telegram /snap bot");
  logWakeCause();
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  // Bring up Wi-Fi for messaging & polling
  WiFi.mode(WIFI_STA);
  const bool wifi_ok = ensureWiFi();

  if (!initCamera()) {
    flashBlink(8, 40, 60);
    if (wifi_ok) {
      telegramSendMessage("Camera could not initiate properly.");
    }
    // Even if camera failed, we still configure sleep so battery isn't drained.
  }

  // Optional hello
  if (wifi_ok) {
    // use the 'cause' defined above, don't redeclare
    switch (cause) {
      case ESP_SLEEP_WAKEUP_EXT1:
        Serial.println("[BOOT] Wake: EXT1 (PIR)");
        sendPirAlertButtons();
        break;
      case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("[BOOT] Wake: EXT0");
        break;
      case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("[BOOT] Wake: TIMER");
        break;
      case ESP_SLEEP_WAKEUP_UNDEFINED:
        Serial.println("[BOOT] Power-on reset");
        telegramSendMessage("🤖 ESP32-CAM awake. You have 90s; send /snap or /ignore.");
        break;
      default:
        Serial.printf("[BOOT] Wake cause: %d\n", (int)cause);
        break;
    } // <-- close switch
  }   // <-- close if (wifi_ok)

  // ---------- ACTIVE WINDOW AFTER WAKE ----------
  // Stay awake for 90 seconds total, polling every ~0.5 seconds.
  const uint32_t AWAKE_MS = 90 * 1000UL;
  const uint32_t POLL_PERIOD_MS = 500UL;
  const uint32_t t_end = millis() + AWAKE_MS;

  while ((int32_t)(t_end - millis()) > 0) {
    if (wifi_ok) {
      pollTelegram();   // <-- your existing single-iteration getUpdates handler
    }
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
