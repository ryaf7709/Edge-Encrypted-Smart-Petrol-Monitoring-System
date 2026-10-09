# Edge-Encrypted Smart Vehicle Fuel & Range Monitoring System

An IoT-based vehicle telemetry monitoring system using ESP32, lightweight cryptography (PRESENT Cipher), ThingSpeak, and Telegram alerts.

## 🚀 Features
- **Real Hardware Sensing:** Automotive fuel sender (Hyundai Accent 2006).
- **Edge Security:** 64-bit block PRESENT cipher (80-bit key, 31 rounds).
- **Dual Display:** Local 16x2 I2C LCD + LEDs, and cloud analytics.
- **Cloud Automation:** Cloud decryption via MATLAB on ThingSpeak and critical alerts via Telegram.

## 🛠️ Tech Stack
- **Hardware:** ESP32, Automotive Fuel Sender, 100Ω Resistor, I2C LCD 16x2.
- **Protocols:** MQTT v3.1.1, I2C, HTTP Webhooks.
- **Cloud & Tools:** Arduino IDE, MATLAB, ThingSpeak, Telegram Bot API.
