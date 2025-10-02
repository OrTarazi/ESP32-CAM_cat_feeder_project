#pragma once
/*
 * secrets.h
 * DO NOT COMMIT THIS FILE.
 * Holds Wi-Fi and Telegram credentials for the Smart Cat Feeder.
 *
 * Steps:
 * 1) Copy secrets.template.h to secrets.h
 * 2) Fill in your real credentials below
 * 3) Add "secrets.h" to your .gitignore
 */

// ---- Wi-Fi ----
#define WIFI_SSID      "WIFI_SSID"
#define WIFI_PASSWORD  "WIFI_PASSWORD"

// ---- Telegram ----
#define BOT_TOKEN  "BOT_TOKEN"
#define CHAT_ID    "CHAT_ID"   // private group or user chat id