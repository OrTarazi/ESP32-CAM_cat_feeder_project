# Smart Cat Feeder – ESP32-CAM + PIR + Telegram

DIY smart cat feeder built on the **ESP32-CAM (AI-Thinker)**.  
Wakes on **PIR motion**, snaps a photo, and talks to you via the **Telegram Bot API**.  
Designed for **battery power** with deep-sleep.
All hardware listed below matches the gear currently on hand.

---

## ✨ Features

- 💤 **Ultra-low power**: deep-sleep most of the time, wake on PIR (EXT1).
- 👀 **Motion trigger**: PIR wakes the ESP → snapshot → Telegram alert.
- 📸 **Remote photo**: `/snap` command captures and returns a fresh image.
- 🚫 **Ignore**: `/ignore` acknowledges the trigger and logs it.
- 🍽️ **Servo hook (planned)**: `/feed` to drive MG996 360° servo for dispensing.
- 🔦 **(Optional) Flash LED**: brief flash for low-light capture.
- 🔐 **Minimal permissions**: bot is scoped to a single chat/group.

> **Note on extras:** Any feature requiring parts beyond the list below (e.g., buzzer/speaker for a deterrent sound) will be explicitly labeled **optional** and **requires additional hardware**.

---

## 🧱 Hardware (what’s actually in use)

- **ESP32-CAM + Antenna** (AI-Thinker)
- **PIR Sensors (1 pcs)** – e.g., HC-SR501 (one used)
- **Servo Motor MG996 – 360° Metal**
- **Breadboards (3 pcs, 400 tie points)** (one used)
- **1× AA Battery Case (4-slot)** (optionally used for PIR or servo rail)
- **1x Blue LED** (turns on when motion detected)
- **1x 200ohm res** to power the LED

![Cat Feeder Setup](photos_and_diagrams/Cat-feeder_assembley.png)

> Everything above is already owned. If a step needs anything else, it will be called out explicitly as “extra”.

---

## 🔌 System Overview

```
[PIR OUT] --(RTC-capable pin)--> [ESP32-CAM]
                     |                |
                     |                +--> [WiFi -> Telegram Bot API]
                     |
                     +-->  [Status LED / Flash control]
                     |
                     +-->  [MG996 360° Servo -> Feeder]
```

- **Flow**: PIR detects motion → ESP32 wakes from deep sleep → capture → send to Telegram.  
- **Chat control**: Send `/snap` for an on-demand image; `/ignore` to dismiss.  
- **Feeding**: `/feed` will rotate MG996 for a calibrated pulse.

---

## 🔧 Build & Flash (Arduino IDE)

1. **Boards manager**: Install “ESP32 by Espressif Systems”.
2. **Board**: `AI Thinker ESP32-CAM`.
3. **Open**: `firmware/cat_feeder.cpp`.
4. **Edit credentials** in the sketch:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI";
   const char* WIFI_PASSWORD = "YOUR_PASS";
   const char* BOT_TOKEN     = "123456789:ABC..."; // from @BotFather
   const char* CHAT_ID       = "-100200300..";   // your group/private chat id
   ```
5. **Programmer**: use the ESP32-CAM programmer/FTDI (IO0 → GND for flashing).
6. **Upload**, then remove IO0-GND and **reset** to run.

> Tip: Disable Bluetooth in code to save power (we do). It’s not used anywhere.

---

## 📲 Telegram Setup

**Commands available today**
- `/snap` → capture new photo

- `/ignore` → acknowledge and do nothing

- `/feed` → drive MG996 360° for a fixed duration (dispense)

![Cat Feeder Setup](photos_and_diagrams/Cat-feeder_workflow.jpg)

---

## 🪫 Power & Wiring Notes (with the gear we have)

- **PIR**  
  - You can power PIR from 5 V **or** its own AA pack (if you isolate rails).  
  - **Common ground is required** between any signal-sharing devices.
- **Servo (MG996 360°)**  
  - Prefer its **own 4×AA pack** (separate from ESP32-CAM)
  - Tie grounds together (servo GND ↔ ESP32-CAM GND) so the PWM reference is shared.

---

## 🧵 Pinout (AI-Thinker ESP32-CAM – as used here)

- **PIR OUT** → `GPIO13` (RTC-capable; used for **EXT1 wake**)
- **Servo signal (planned)** → `GPIO14`
- **(Optional) Flash LED / Status** → `GPIO4`

*(PIR Vcc → 5 V or its own pack; Servo Vcc → own 4×AA; both with common GND to ESP32-CAM.)*

---

## 🐞 Troubleshooting

- **DMA overflow / capture failed**:  
  Lower frame size (e.g., `FRAMESIZE_SVGA → VGA/QVGA`) and try again. Ensure stable 5 V.
- **No Telegram messages**:  
  Check Wi-Fi credentials; verify chat id; token still valid; TLS handshake prints OK.
- **Random resets on Wi-Fi TX**:  
  Add more bulk capacitance on the 5 V rail; shorten wires; check buck thermal.

---

## 🗺️ Roadmap

- [x] PIR wake from deep sleep + snapshot
- [x] Telegram photo upload
- [x] `/snap` and `/ignore` commands
- [x] `/feed` command to drive MG996 360°
- [x] “Going back to sleep” Telegram status on deep-sleep entry
- [x] Motion “two-button” message (simulate trigger on boot for testing)
- [x] Power profiling & sleep current reduction
- [ ] (Optional) Sound deterrent **(requires extra buzzer – not in current parts)**

---

## 📜 License

© Or Tarazi, 2025.  
Feel free to use, modify, and share. Attribution appreciated.

---

## 🙌 Acknowledgments

- ESP32-CAM community examples and docs  
- Telegram Bot API docs  
- Cirkit Designer IDE
- Confluence
