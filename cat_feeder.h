#ifndef CAT_FEEDER_H
#define CAT_FEEDER_H

#pragma once
#include "user_wifi_and_telegram_config.h"
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_bt.h"   // to disable BT for power saving (optional)
#include "esp_sleep.h"
#include "driver/rtc_io.h"



/**
 * @file    cat_feeder.h
 * @brief   Public API and documentation for the Smart Cat Feeder (ESP32-CAM + PIR + Telegram).
 * @author  Or Tarazi
 * @date    September 2025
 *
 * @details
 * This header exposes the functions used by the Smart Cat Feeder firmware:
 * - Wi-Fi bring-up and status helpers
 * - Telegram HTTPS utilities (messages, inline buttons, photo upload)
 * - Camera initialization and capture pipeline (OV2640 via esp_camera)
 * - Main Arduino entry points (setup/loop)
 *
 * @note
 *  - Fill your credentials in `user_wifi_and_telegram_config.h` (SSID/PASS, BOT_TOKEN, CHAT_ID).
 *  - TLS is used with `client.setInsecure()` for simplicity (OK for hobby use; not for production).
 *  - JSON handling is done via naive string scanning (no ArduinoJson dependency).
 *  - PIR wake is configured via EXT1; see implementation for RTC GPIO setup.
 *
 * @warning
 *  - The code performs network I/O on the main thread; avoid blocking for long to keep the
 *    watchdog happy. Default timeouts are modest (6–8 seconds).
 *  - Ensure adequate power rails and decoupling for ESP32-CAM and any servo you attach.
 */

/** @name Public constants imported from the implementation
 *  
 */


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
extern uint32_t lastPollMs;
extern long     lastUpdateId;



/**
 * @brief Default photo caption sent with snapshots.
 *
 * Declared here for use as the default parameter of `takeAndSendPhoto()`.
 * Defined in the implementation unit (cat_feeder.cpp).
 */
extern const char* PHOTO_CAPTION;
/** @} */


/**
 * @brief Blink the onboard flash LED for basic visual feedback.
 *
 * @param times   Number of blink cycles (default: 2).
 * @param on_ms   LED ON duration in milliseconds per blink (default: 60 ms).
 * @param off_ms  LED OFF duration in milliseconds between blinks (default: 120 ms).
 *
 * @details
 * Useful to signal milestones such as boot, error, or successful operations without
 * opening the serial monitor.
 */
void flashBlink(int times = 2, int on_ms = 60, int off_ms = 120);



/**
 * @brief Ensure the ESP32 is connected to Wi-Fi (STA mode).
 *
 * @return true  if already connected or (re)connection succeeded,
 * @return false if connection attempts failed within the built-in retry window.
 *
 * @details
 * - Performs a quick reconnect sequence if disconnected.
 * - Prints local IP on success and current RSSI (dBm).
 * - Requires `WIFI_SSID` / `WIFI_PASSWORD` to be defined in your user config header.
 */
bool ensureWiFi();


/** 
 * @brief Send a short message and present inline buttons: **snap** and **ignore**.
 *
 * @details
 * Uses Telegram's inline keyboard with `callback_data` values `"snap"` and `"ignore"`.
 * The handler logic (parsing callbacks or `/snap` messages) is implemented in polling.
 */
void sendPirAlertButtons();

/**
 * @brief Send a plain text message to the configured Telegram chat.
 *
 * @param text  Message body (will be URL-encoded).
 * @return true on successful HTTP exchange (does not validate JSON success),
 * @return false on connection/IO errors.
 *
 * @note Uses HTTPS to `api.telegram.org` with `setInsecure()`.
 */
bool telegramSendMessage(const String& text);

/**
 * @brief Send a text message with two inline buttons (**snap**, **ignore**).
 *
 * @param text  Prompt shown above the inline keyboard.
 * @return true on successful HTTP exchange (does not validate JSON success),
 * @return false on connection/IO errors.
 */
bool telegramSendMessageWithButtons(const String& text);

/**
 * @brief Upload a JPEG frame to Telegram via `sendPhoto` (multipart/form-data).
 *
 * @param jpg_buf   Pointer to JPEG buffer.
 * @param jpg_len   Length of the JPEG buffer in bytes.
 * @param caption   Optional text shown with the photo (may be empty).
 * @return true on successful HTTP exchange (does not validate JSON success),
 * @return false on connection/IO errors or stalled write.
 *
 * @details
 * Streams the JPEG in chunks to avoid large RAM copies. Prints the HTTP response lines
 * to the serial monitor for debugging.
 */
bool telegramSendPhoto(const uint8_t* jpg_buf, size_t jpg_len, const char* caption);
/** @} */

/** @name String utilities
 *  @{
 */
/**
 * @brief Minimal URL-encoder for Telegram GET query parameters.
 *
 * @param s  Input string.
 * @return   URL-encoded string where non-alphanumerics are percent-escaped,
 *           and spaces become `%20`.
 *
 * @note This is not a fully RFC-compliant encoder but is sufficient for typical text.
 */
String urlencode(const String& s);
/** @} */

/** @name Camera setup and capture
 * 
 *
 *
 * @brief Initialize the ESP32-CAM (OV2640) with sensible defaults.
 *
 * @return true on successful `esp_camera_init`, false otherwise.
 *
 * @details
 * - Checks for PSRAM; if present, configures VGA and quality settings accordingly.
 * - Adjusts sensor framesize/quality for a balance between size and clarity.
 * - Prints error code (`esp_err_t`) on failure.
 */
bool initCamera();

/**
 * @brief Capture a *fresh* JPEG frame and send it to Telegram.
 *
 * @param caption         Optional caption (defaults to `PHOTO_CAPTION`).
 * @param fresh_wait_ms   Milliseconds to wait after discarding a possibly stale frame
 *                        before grabbing a new one (default: 120 ms).
 * @return true if the frame was captured and the upload request was issued successfully,
 * @return false on capture or network errors.
 *
 * @details
 * Implements a "flush & grab" technique:
 *  1. Grabs the current frame (if any) and discards it (records its timestamp).
 *  2. Waits `fresh_wait_ms` to allow the driver to produce the *next* frame.
 *  3. Captures the fresh frame and uploads it via `telegramSendPhoto()`.
 * Always returns the frame buffer to the camera driver.
 */
bool takeAndSendPhoto(const char* caption = PHOTO_CAPTION, uint32_t fresh_wait_ms = 120);
/** @} */

/** @name Telegram polling
 *  @{
 */
/**
 * @brief Poll Telegram updates via `getUpdates` and handle `/snap` commands for the configured chat.
 *
 * @details
 * - Uses a stored `lastUpdateId` offset to avoid re-processing older updates.
 * - Naively scans the JSON string for `/snap` occurrences within the correct `chat.id`.
 * - On `/snap`, sends an acknowledgment message and triggers `takeAndSendPhoto()`.
 *
 * @note Designed to be called periodically (e.g., every ~3 seconds during the awake window).
 *       Requires Wi-Fi connectivity; function returns early if Wi-Fi is down.
 */
void pollTelegram();
/** @} */

/** @name Arduino entry points
 *  @{
 */
/**
 * @brief Arduino setup routine: initializes serial, disables BT, connects Wi-Fi,
 *        initializes camera, notifies Telegram, runs a short active polling window,
 *        then configures EXT1 wake (PIR HIGH) and enters deep sleep.
 *
 * @details
 * - Prints the wakeup cause.
 * - Active window length and polling cadence are defined in the implementation.
 * - Before sleeping, Wi-Fi is shut down and a "going to sleep" message is sent.
 */
void setup();

/**
 * @brief Arduino main loop (unused here).
 *
 * @details
 * Execution should not reach a recurring loop because the device enters deep sleep
 * from `setup()` after the active window. This function is intentionally empty.
 */
void loop();
/** @} */

void enterDeepSleep();

void logWakeCause();

/* -----------------------------------------------------------------------
   Internal helpers (documented for completeness; not part of the public API)

   The following functions are implemented as `static` in the .cpp to restrict
   their linkage to the translation unit. They are documented here but not
   declared publicly to avoid ODR/linkage mismatches:

   - static long  extractMaxUpdateId(const String& body);
       Scans the Telegram JSON string for all `"update_id":<num>` occurrences and
       returns the maximum value found (or the current `lastUpdateId` if none).

   - static bool  bodyContainsSnapForMe(const String& body);
       Checks if the Telegram JSON contains a `/snap` message (or callback) that
       belongs to the configured `CHAT_ID`. Uses a small sliding window around the
       found token to associate it with the correct `"chat":{"id":...}`.

   - static void  configurePirRtcInput();
       Configures the PIR GPIO as an RTC input (INPUT_ONLY) with pulldown enabled,
       appropriate for EXT1 high-level wake.

   - static void  logWakeCause();
       Prints the wakeup cause decoded from `esp_sleep_get_wakeup_cause()`.

   - static void  enterDeepSleep();
       Enables EXT1 wake on PIR high, prints a status line, and starts deep sleep.
   ----------------------------------------------------------------------- */

#endif // CAT_FEEDER_H
