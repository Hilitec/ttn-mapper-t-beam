/*
 * ============================================================================
 * Project      : The Things Network LoRaWAN Mapper
 * Version      : 1.0.0
 * Release date : 2026-11-13
 * Copyright    : (C) 2026 Hilitec
 * Author       : Andre Liechti
 * License      : GNU General Public License v3.0 or later
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Repository   : https://github.com/hilitec/ttn-mapper-t-beam
 *
 * DESCRIPTION
 *
 * An ESP32-based LoRaWAN GPS tracker and mapper designed for The Things
 * Network (TTN) and TTN Mapper.
 *
 * The device uses a LilyGo T-Beam V1.2 with an SX1276 LoRa transceiver
 * and an integrated GPS receiver to periodically transmit its geographical
 * position and battery voltage over LoRaWAN using OTAA authentication.
 *
 * Key features:
 *   - Configuration through Wi-Fi portal or USB serial commands
 *   - Configurable DevEUI, AppEUI (JoinEUI) and AppKey
 *   - LoRaWAN credentials stored persistently in ESP32 NVS,
 *     with support for post-deployment configuration and updates
 *   - LoRaWAN OTAA activation with persistent DevNonce management
 *     to prevent nonce reuse across device reboots
 *   - GPS position tracking using TinyGPSPlus
 *   - CayenneLPP payload encoding
 *   - Battery voltage monitoring
 *   - OLED status display
 *   - On-demand Wi-Fi configuration portal
 *   - Automatic Wi-Fi shutdown after configuration
 *   - Automatic OTAA join retries when activation fails
 *   - Movement-based GPS reporting using a configurable distance threshold
 *   - Configurable stationary reporting interval
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see:
 * https://www.gnu.org/licenses/gpl-3.0.html
 *
 * ============================================================================
 *
 * DEVICE CONFIGURATION
 *
 * LoRaWAN credentials
 * -------------------
 * DevEUI, AppEUI (JoinEUI) and AppKey can be configured or updated
 * after deployment through:
 *   - Wi-Fi configuration portal (TBeam-Mapper-Setup)
 *   - USB serial commands
 *
 * All credentials are stored persistently in ESP32 Non-Volatile Storage
 * (NVS) and are retained across reboots.
 *
 * Initial setup
 * -------------
 * When valid LoRaWAN credentials are not configured, or when the user
 * button is pressed during boot, an Access Point with the SSID
 * "TBeam-Mapper-Setup" is enabled for 180 seconds.
 *
 * Configuration portal:
 *   http://192.168.4.1
 *
 * Wi-Fi is disabled after configuration is completed.
 *
 * GPS reporting
 * -------------
 * The device checks its GPS position every 60 seconds.
 * An uplink is sent when the distance from the last transmitted position
 * reaches the configured movement threshold, or when the configured
 * stationary reporting interval has elapsed.
 *
 * Default settings:
 *   Movement threshold: 25 meters
 *   Stationary interval: 15 minutes
 *
 * USB serial commands (115200 baud)
 * ---------------------------------
 *   INFO
 *     Display the configured identifiers and GPS reporting settings.
 *
 *   SET_DEVEUI=<16 hexadecimal characters>
 *   SET_APPEUI=<16 hexadecimal characters>
 *   SET_APPKEY=<32 hexadecimal characters>
 *     Update a LoRaWAN credential and reboot the device.
 *
 *   SET_MOVE_THRESHOLD=<meters>
 *     Set the minimum movement distance (1 to 10000 meters).
 *
 *   SET_STATIONARY_INTERVAL=<minutes>
 *     Set the maximum interval between stationary reports (1 to 1440 minutes).
 *
 *   PORTAL
 *     Open the Wi-Fi configuration portal.
 *
 *   REBOOT
 *     Reboot the device.
 *
 * APPLIANCE CONFIGURATION
 *
 * Payload formatter
 * -----------------
 * Format: CayenneLPP
 *
 * Webhooks
 * --------
 * TTN Mapper
 *   Forward format: JSON
 *   Base URL: https://integrations.ttnmapper.org/tts/v3
 *
 * LORAWAN CONFIGURATION
 *
 * Frequency plan:
 *   Europe 863-870 MHz (EU868)
 *
 * LoRaWAN version:
 *   LoRaWAN Specification 1.0.4
 *
 * Regional Parameters:
 *   RP002 Regional Parameters 1.0.4
 *
 * Hardware:
 *   LilyGo T-Beam V1.2
 *     with SX1276 LoRa transceiver
 *
 * DEPENDENCIES
 *   - Arduino framework (ESP32)
 *   - RadioLib
 *   - TinyGPSPlus
 *   - XPowersLib
 *   - WiFiManager
 *   - U8g2
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <U8g2lib.h>
#include <string.h>
#include <stdlib.h>

// --- LilyGo T-Beam V1.2 (SX1276) ---
#define I2C_SDA         21
#define I2C_SCL         22
#define LORA_CS         18
#define LORA_DIO0       26
#define LORA_RST        23
#define LORA_DIO1       33
#define GPS_RX          34
#define GPS_TX          12
#define USER_BUTTON     38

// The last DevNonce accepted by TTN in the supplied logs was 0x0028.
// RadioLib 6.6 stores the next DevNonce to use in its nonces buffer.
static const uint16_t INITIAL_DEV_NONCE = 0x0029;

// --- Instances ---
XPowersAXP2101 PMU;
SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
LoRaWANNode node(&radio, &EU868);
TinyGPSPlus gps;
Preferences prefs;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- Global variables ---
String strDevEUI, strAppEUI, strAppKey;
String loraStatus = "Ready";
uint32_t txCount = 0;
uint32_t txIntervalSec = 60;
uint16_t moveThresholdMeters = 25;
uint16_t stationaryIntervalMinutes = 15;
uint8_t appKeyBytes[16] = {};

static bool hasLastTxPosition = false;
static double lastTxLatitude = 0.0;
static double lastTxLongitude = 0.0;
static double lastMovementMeters = 0.0;
static uint32_t lastSuccessfulTx = 0;

static const size_t NONCES_SIZE = RADIOLIB_LORAWAN_NONCES_BUF_SIZE;
static const size_t SESSION_SIZE = RADIOLIB_LORAWAN_SESSION_BUF_SIZE;

static const uint8_t PENDING_NONE = 0;
static const uint8_t PENDING_JOIN = 1;
static const uint8_t PENDING_UPLINK = 2;

bool nonceRestoreFatal = false;

// --- OLED display ---
void updateOLED(
  const String& line1,
  const String& line2,
  const String& line3,
  const String& line4
) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x8_tf);
  u8g2.drawStr(0, 8, line1.c_str());
  u8g2.drawStr(0, 22, line2.c_str());
  u8g2.drawStr(0, 36, line3.c_str());
  u8g2.drawStr(0, 50, line4.c_str());
  u8g2.sendBuffer();
}

// --- LoRaWAN buffer helpers (RadioLib 6.6.x) ---
// RadioLib 6.6 stores 16-bit values little-endian and uses an XOR checksum
// over big-endian 16-bit words for its buffer integrity signatures.

uint16_t radioLibChecksum16(const uint8_t* data, size_t len) {
  uint16_t checksum = 0;

  for (size_t i = 0; i + 1 < len; i += 2) {
    checksum ^= (
      (static_cast<uint16_t>(data[i]) << 8) |
      static_cast<uint16_t>(data[i + 1])
    );
  }

  if ((len & 1U) != 0U) {
    checksum ^= static_cast<uint16_t>(data[len - 1]) << 8;
  }

  return checksum;
}

void writeRadioLibUint16(uint8_t* buffer, uint16_t value) {
  buffer[0] = static_cast<uint8_t>(value & 0xFF);
  buffer[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint16_t readRadioLibUint16(const uint8_t* buffer) {
  return static_cast<uint16_t>(buffer[0]) |
    (static_cast<uint16_t>(buffer[1]) << 8);
}

// --- GPS distance and configuration helpers ---
double haversineMeters(
  double lat1,
  double lon1,
  double lat2,
  double lon2
) {
  constexpr double EARTH_RADIUS_M = 6371000.0;
  constexpr double DEG_TO_RAD_FACTOR = 0.017453292519943295;

  double dLat = (lat2 - lat1) * DEG_TO_RAD_FACTOR;
  double dLon = (lon2 - lon1) * DEG_TO_RAD_FACTOR;

  lat1 *= DEG_TO_RAD_FACTOR;
  lat2 *= DEG_TO_RAD_FACTOR;

  double sinHalfLat = sin(dLat / 2.0);
  double sinHalfLon = sin(dLon / 2.0);

  double a = sinHalfLat * sinHalfLat +
    cos(lat1) * cos(lat2) * sinHalfLon * sinHalfLon;

  a = constrain(a, 0.0, 1.0);

  return EARTH_RADIUS_M * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

bool parseUnsignedSetting(
  const char* value,
  uint16_t minimum,
  uint16_t maximum,
  uint16_t& result
) {
  if (value == nullptr || *value == '\0') {
    return false;
  }

  char* end = nullptr;
  unsigned long parsed = strtoul(value, &end, 10);

  if (*end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }

  result = static_cast<uint16_t>(parsed);
  return true;
}

// --- LoRaWAN NVS storage ---
// Two alternating slots protect the last complete copy if power is lost
// while a new copy is being written.
//
// NVS keys used by this code:
//   slot : active slot (0 or 1)
//   n0/n1: RadioLib nonces buffers
//   s0/s1: RadioLib session buffers
//   op   : interrupted operation marker
//
// op = 1: JoinRequest may have been transmitted.
// op = 2: uplink may have been transmitted.

bool setPendingOperation(uint8_t operation) {
  prefs.begin("lorawan", false);
  size_t written = prefs.putUChar("op", operation);
  prefs.end();

  return written == sizeof(uint8_t);
}

bool saveLoRaWANNoncesOnly() {
  uint8_t nonces[NONCES_SIZE];

  memcpy(nonces, node.getBufferNonces(), sizeof(nonces));

  prefs.begin("lorawan", false);

  uint8_t currentSlot = prefs.getUChar("slot", 0);
  uint8_t targetSlot = currentSlot ^ 1U;

  const char* nonceKey = targetSlot ? "n1" : "n0";
  const char* sessionKey = targetSlot ? "s1" : "s0";

  bool success =
    prefs.putBytes(nonceKey, nonces, sizeof(nonces)) == sizeof(nonces);

  if (success) {
    prefs.remove(sessionKey);
    success = prefs.putUChar("slot", targetSlot) == sizeof(uint8_t);
  }

  prefs.end();

  if (!success) {
    Serial.println("[ERROR] Failed to save LoRaWAN nonces.");
  }

  return success;
}

bool saveLoRaWANState() {
  if (!node.isActivated()) {
    Serial.println("[ERROR] Cannot save an inactive LoRaWAN session.");
    return false;
  }

  uint8_t nonces[NONCES_SIZE];
  uint8_t session[SESSION_SIZE];

  memcpy(nonces, node.getBufferNonces(), sizeof(nonces));
  memcpy(session, node.getBufferSession(), sizeof(session));

  prefs.begin("lorawan", false);

  uint8_t currentSlot = prefs.getUChar("slot", 0);
  uint8_t targetSlot = currentSlot ^ 1U;

  const char* nonceKey = targetSlot ? "n1" : "n0";
  const char* sessionKey = targetSlot ? "s1" : "s0";

  bool success =
    prefs.putBytes(nonceKey, nonces, sizeof(nonces)) == sizeof(nonces);

  if (success) {
    success =
      prefs.putBytes(sessionKey, session, sizeof(session)) == sizeof(session);
  }

  // Make the new slot active only after both buffers have been written.
  if (success) {
    success = prefs.putUChar("slot", targetSlot) == sizeof(uint8_t);
  }

  prefs.end();

  if (!success) {
    Serial.println("[ERROR] LoRaWAN NVS transaction failed.");
  }

  return success;
}

bool createInitialLoRaWANNonces() {
  uint8_t nonces[NONCES_SIZE] = {};

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_VERSION],
    RADIOLIB_LORAWAN_NONCES_VERSION_VAL
  );

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_MODE],
    RADIOLIB_LORAWAN_MODE_OTAA
  );

  nonces[RADIOLIB_LORAWAN_NONCES_CLASS] = RADIOLIB_LORAWAN_CLASS_A;
  nonces[RADIOLIB_LORAWAN_NONCES_PLAN] = EU868.bandNum;

  // RadioLib 6.6.0 calls clearNonces() at the end of beginOTAA().
  // In this release, clearNonces() resets keyCheckSum to zero. The
  // NONCES_CHECKSUM field must therefore also be zero for setBufferNonces()
  // to accept this initial buffer.
  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_CHECKSUM],
    0
  );

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_DEV_NONCE],
    INITIAL_DEV_NONCE
  );

  // JoinNonce and ACTIVE are initially zero.
  uint16_t signature = radioLibChecksum16(
    nonces,
    NONCES_SIZE - sizeof(uint16_t)
  );

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_SIGNATURE],
    signature
  );

  int state = node.setBufferNonces(nonces);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf(
      "[ERROR] Initial RadioLib nonces buffer rejected: %d\n",
      state
    );
    return false;
  }

  Serial.printf(
    "[LORA] Initial DevNonce set to 0x%04X.\n",
    INITIAL_DEV_NONCE
  );

  return saveLoRaWANNoncesOnly();
}

// Skip one DevNonce after a reset interrupted a JoinRequest.
// This is conservative: the nonce may not have gone over the air, but
// skipping it is harmless and avoids reusing it if it did.
bool skipInterruptedJoinNonce() {
  uint8_t nonces[NONCES_SIZE];

  memcpy(nonces, node.getBufferNonces(), sizeof(nonces));

  uint16_t currentDevNonce = readRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_DEV_NONCE]
  );

  // Keep away from the wraparound value. DevNonce must never wrap to zero.
  if (currentDevNonce >= 0xFFFE) {
    Serial.println("[ERROR] DevNonce exhausted.");
    return false;
  }

  uint16_t nextDevNonce = currentDevNonce + 1;

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_DEV_NONCE],
    nextDevNonce
  );

  uint16_t signature = radioLibChecksum16(
    nonces,
    NONCES_SIZE - sizeof(uint16_t)
  );

  writeRadioLibUint16(
    &nonces[RADIOLIB_LORAWAN_NONCES_SIGNATURE],
    signature
  );

  int state = node.setBufferNonces(nonces);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ERROR] Cannot advance DevNonce: %d\n", state);
    return false;
  }

  Serial.printf(
    "[LORA] Skipped interrupted DevNonce; next value: 0x%04X.\n",
    nextDevNonce
  );

  return saveLoRaWANNoncesOnly();
}

bool restoreLoRaWANState() {
  uint8_t nonces[NONCES_SIZE];
  uint8_t session[SESSION_SIZE];

  prefs.begin("lorawan", true);

  bool hasSlot = prefs.isKey("slot");
  uint8_t slot = prefs.getUChar("slot", 0);
  uint8_t pendingOperation = prefs.getUChar("op", PENDING_NONE);

  const char* nonceKey = slot ? "n1" : "n0";
  const char* sessionKey = slot ? "s1" : "s0";

  bool hasNonces =
    prefs.getBytesLength(nonceKey) == sizeof(nonces);

  bool hasSession =
    prefs.getBytesLength(sessionKey) == sizeof(session);

  if (hasNonces) {
    prefs.getBytes(nonceKey, nonces, sizeof(nonces));
  }

  if (hasSession) {
    prefs.getBytes(sessionKey, session, sizeof(session));
  }

  prefs.end();

  if (!hasSlot || !hasNonces) {
    Serial.println("[LORA] No saved nonces found.");

    if (!createInitialLoRaWANNonces()) {
      nonceRestoreFatal = true;
    }

    return false;
  }

  int state = node.setBufferNonces(nonces);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[ERROR] Nonces restore failed: %d\n", state);
    nonceRestoreFatal = true;
    return false;
  }

  if (pendingOperation == PENDING_JOIN) {
    Serial.println("[LORA] Interrupted JoinRequest detected.");

    if (!skipInterruptedJoinNonce()) {
      nonceRestoreFatal = true;
      return false;
    }

    setPendingOperation(PENDING_NONE);
    return false;
  }

  if (pendingOperation == PENDING_UPLINK) {
    Serial.println("[LORA] Interrupted uplink detected; session discarded.");

    // setBufferNonces() leaves the node inactive. Persist that state without
    // restoring the old session, because its FCntUp may be stale.
    if (!saveLoRaWANNoncesOnly()) {
      nonceRestoreFatal = true;
      return false;
    }

    setPendingOperation(PENDING_NONE);
    return false;
  }

  // Temporarily disable LoRaWAN session restoration.
  Serial.println("[LORA] Session restoration disabled.");
  Serial.println("[LORA] Nonces restored; fresh OTAA join required.");

  return false;

  // Original session restoration code retained for later reactivation.
  if (!hasSession) {
    Serial.println("[LORA] Nonces restored; no saved session.");
    return false;
  }

  state = node.setBufferSession(session);

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[LORA] Session restore failed: %d\n", state);

    // Keep the nonces, discard only the incompatible session.
    if (!saveLoRaWANNoncesOnly()) {
      nonceRestoreFatal = true;
    }

    return false;
  }

  state = node.activateOTAA();

  if (state == RADIOLIB_LORAWAN_SESSION_RESTORED &&
      node.isActivated()) {
    Serial.println("[LORA] Session restored successfully.");
    return true;
  }

  Serial.printf("[LORA] Session activation failed: %d\n", state);

  if (!saveLoRaWANNoncesOnly()) {
    nonceRestoreFatal = true;
  }

  return false;
}

// --- Hexadecimal helpers ---
void hexStringToBytes(const String& hex, uint8_t* bytes, size_t len) {
  for (size_t i = 0; i < len; i++) {
    *(bytes + i) = strtoul(
      hex.substring(i * 2, i * 2 + 2).c_str(),
      nullptr,
      16
    );
  }
}

uint64_t hexStringToUint64(const String& hex) {
  return strtoull(hex.c_str(), nullptr, 16);
}

// --- Power supply management V1.2 ---
void initPMU() {
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!PMU.begin(Wire, AXP2101_SLAVE_ADDRESS, I2C_SDA, I2C_SCL)) {
    Serial.println("[ERROR] AXP2101 chip unavailable !");
    while (1);
  }

  PMU.setALDO1Voltage(3300);
  PMU.enableALDO1();

  PMU.setALDO2Voltage(3300);
  PMU.enableALDO2();

  // GPS u-blox power supply (ALDO3, 3.3V)
  PMU.setALDO3Voltage(3300);
  PMU.enableALDO3();

  // GPS u-blox power supply (BLD01, 3.3V)
  // PMU.setBLDO1Voltage(3300);
  // PMU.enableBLDO1();

  delay(250);
}

// --- Wi-Fi configuration portal ---
// Intentionally preserved from the supplied original version.
void startConfigPortal() {
  Serial.println("[WIFI] Access Point enabled: TBeam-Mapper-Setup");
  updateOLED("WIFI CONFIG MODE", "SSID: TBeam-Mapper-Setup", "IP: 192.168.4.1", "Please define keys");

  WiFiManager wm;

  // Expose only the LoRaWAN parameter page and portal exit.
  const char* menu[] = {"param"};
  wm.setMenu(menu, 1);
  wm.setTitle("LoRaWAN Configuration");
  wm.setCustomHeadElement(
    "<script>"
    "document.addEventListener('DOMContentLoaded',function(){"
    "document.querySelectorAll(\"form[action='/param'] button\").forEach(function(button){"
    "button.textContent='Configure LoRaWAN';"
    "});"
    "});"
    "</script>"
  );

  char moveThresholdValue[8];
  char stationaryIntervalValue[8];

  snprintf(
    moveThresholdValue,
    sizeof(moveThresholdValue),
    "%u",
    static_cast<unsigned int>(moveThresholdMeters)
  );

  snprintf(
    stationaryIntervalValue,
    sizeof(stationaryIntervalValue),
    "%u",
    static_cast<unsigned int>(stationaryIntervalMinutes)
  );

  WiFiManagerParameter p_deveui("deveui", "DevEUI (16 hex)", strDevEUI.c_str(), 16);
  WiFiManagerParameter p_appeui("appeui", "AppEUI / JoinEUI (16 hex)", strAppEUI.c_str(), 16);
  WiFiManagerParameter p_appkey("appkey", "AppKey (32 hex)", strAppKey.c_str(), 32);
  WiFiManagerParameter p_move_threshold(
    "move_threshold",
    "Movement threshold (meters, 1-10000)",
    moveThresholdValue,
    7
  );
  WiFiManagerParameter p_stationary_interval(
    "stationary_interval",
    "Stationary interval (minutes, 1-1440)",
    stationaryIntervalValue,
    7
  );

  wm.addParameter(&p_deveui);
  wm.addParameter(&p_appeui);
  wm.addParameter(&p_appkey);
  wm.addParameter(&p_move_threshold);
  wm.addParameter(&p_stationary_interval);
  wm.setConfigPortalTimeout(180);

  if (!wm.startConfigPortal("TBeam-Mapper-Setup")) {
    updateOLED("PORTAL TIME EXPIRED", "Rebooting...", "", "");
    delay(1000);
    ESP.restart();
  }

  uint16_t newMoveThreshold;
  uint16_t newStationaryInterval;

  prefs.begin("lorawan", false);
  prefs.putString("deveui", p_deveui.getValue());
  prefs.putString("appeui", p_appeui.getValue());
  prefs.putString("appkey", p_appkey.getValue());

  if (parseUnsignedSetting(
        p_move_threshold.getValue(),
        1,
        10000,
        newMoveThreshold
      )) {
    prefs.putUShort("move_m", newMoveThreshold);
  } else {
    Serial.println("[WIFI] Invalid movement threshold; previous value kept.");
  }

  if (parseUnsignedSetting(
        p_stationary_interval.getValue(),
        1,
        1440,
        newStationaryInterval
      )) {
    prefs.putUShort("still_min", newStationaryInterval);
  } else {
    Serial.println("[WIFI] Invalid stationary interval; previous value kept.");
  }

  prefs.end();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  updateOLED("KEYS SAVED", "Wi-FI disabled", "Rebooting...", "");
  delay(1500);
  ESP.restart();
}

// --- Serial USB commands ---
void checkSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    prefs.begin("lorawan", false);

    if (cmd.startsWith("SET_DEVEUI=")) {
      prefs.putString("deveui", cmd.substring(11));
      Serial.println("DevEUI stored.");

      prefs.end();
      ESP.restart();
      return;

    } else if (cmd.startsWith("SET_APPEUI=")) {
      prefs.putString("appeui", cmd.substring(11));
      Serial.println("AppEUI stored.");

      prefs.end();
      ESP.restart();
      return;

    } else if (cmd.startsWith("SET_APPKEY=")) {
      prefs.putString("appkey", cmd.substring(11));
      Serial.println("AppKey stored.");

      prefs.end();
      ESP.restart();
      return;

    } else if (cmd.startsWith("SET_MOVE_THRESHOLD=")) {
      uint16_t value;

      if (parseUnsignedSetting(cmd.substring(19).c_str(), 1, 10000, value)) {
        if (prefs.putUShort("move_m", value) == sizeof(uint16_t)) {
          moveThresholdMeters = value;
          Serial.printf("[CONFIG] Movement threshold set to %u meters.\n", static_cast<unsigned int>(value));
        } else {
          Serial.println("[ERROR] Cannot save movement threshold.");
        }
      } else {
        Serial.println("[ERROR] Movement threshold must be between 1 and 10000 meters.");
      }

    } else if (cmd.startsWith("SET_STATIONARY_INTERVAL=")) {
      uint16_t value;

      if (parseUnsignedSetting(cmd.substring(24).c_str(), 1, 1440, value)) {
        if (prefs.putUShort("still_min", value) == sizeof(uint16_t)) {
          stationaryIntervalMinutes = value;
          Serial.printf("[CONFIG] Stationary interval set to %u minutes.\n", static_cast<unsigned int>(value));
        } else {
          Serial.println("[ERROR] Cannot save stationary interval.");
        }
      } else {
        Serial.println("[ERROR] Stationary interval must be between 1 and 1440 minutes.");
      }

    } else if (cmd == "PORTAL") {
      prefs.end();
      startConfigPortal();
      return;

    } else if (cmd == "REBOOT") {
      prefs.end();
      ESP.restart();
      return;

    } else if (cmd == "INFO") {
      Serial.println("DevEUI: " + prefs.getString("deveui", "Vide"));
      Serial.println("AppEUI: " + prefs.getString("appeui", "Vide"));
      Serial.println("AppKey: ********");
      Serial.printf("Movement threshold: %u meters\n", static_cast<unsigned int>(moveThresholdMeters));
      Serial.printf("Stationary interval: %u minutes\n", static_cast<unsigned int>(stationaryIntervalMinutes));
    }

    prefs.end();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(USER_BUTTON, INPUT); // GPIO 38 has already an external pull-up on the T-Beam

  initPMU();

  u8g2.begin();
  updateOLED("T-Beam LoRaWAN Mapper", "Rebooting...", "", "");

  Serial1.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  prefs.begin("lorawan", true);
  strDevEUI = prefs.getString("deveui", "");
  strAppEUI = prefs.getString("appeui", "");
  strAppKey = prefs.getString("appkey", "");
  moveThresholdMeters = prefs.getUShort("move_m", 25);
  stationaryIntervalMinutes = prefs.getUShort("still_min", 15);
  prefs.end();

  if (digitalRead(USER_BUTTON) == LOW ||
      strDevEUI.length() != 16 ||
      strAppEUI.length() != 16 ||
      strAppKey.length() != 32) {
    startConfigPortal();
  }

  updateOLED("T-Beam LoRaWAN Mapper", "Initializing SX1276...", "", "");

  int state = radio.begin();

  if (state != RADIOLIB_ERR_NONE) {
    updateOLED("RADIO ERROR", "Code: " + String(state), "", "");
    Serial.printf("[ERROR] radio.begin() : %d\n", state);

    while (1);
  }

  uint64_t joinEUI = hexStringToUint64(strAppEUI);
  uint64_t devEUI = hexStringToUint64(strDevEUI);

  hexStringToBytes(strAppKey, appKeyBytes, sizeof(appKeyBytes));

  // LoRaWAN 1.0.4 needs the same key for NwkKey and AppKey.
  node.beginOTAA(joinEUI, devEUI, appKeyBytes, appKeyBytes);

  // Manual Data Rate for moving devices
  // DR0 SF12BW125
  // DR1 SF11BW125
  // DR2 SF10BW125
  // DR3 SF9BW125
  // DR4 SF8BW125
  // DR5 SF7BW125
  node.setADR(false);
  node.setDatarate(2);

  bool restored = restoreLoRaWANState();

  if (nonceRestoreFatal) {
    updateOLED("LORAWAN ERROR", "Nonce restore failed", "Check NVS / keys", "");

    while (1);
  }

  if (restored) {
    loraStatus = "Session restored";
    Serial.println("[LORA] Existing session restored.");
  } else {
    loraStatus = "Join required";
    Serial.println("[LORA] Ready for Join OTAA.");
  }
}

void loop() {
  checkSerialCommands();

  while (Serial1.available() > 0) {
    char c = Serial1.read();
    // Serial.write(c); // Display NMEA raw frames ($GPGGA, $GPRMC...)
    gps.encode(c);
  }

  // Trying Join OTAA every 15 seconds while not active.
  static uint32_t lastJoinAttempt = 0;

  if (!node.isActivated() &&
      (millis() - lastJoinAttempt > 15000 || lastJoinAttempt == 0)) {

    lastJoinAttempt = millis();

    Serial.println("[LORA] Sending JoinRequest OTAA...");
    updateOLED(
      "LoRa: Join OTAA...",
      "Trying...",
      "DevEUI: " + strDevEUI.substring(10),
      ""
    );

    // Persist an operation marker before touching the radio.
    if (!setPendingOperation(PENDING_JOIN)) {
      Serial.println("[ERROR] Cannot persist JoinRequest marker.");
      while (1);
    }

    uint16_t nextDevNonce = readRadioLibUint16(
      &node.getBufferNonces()[RADIOLIB_LORAWAN_NONCES_DEV_NONCE]
    );

    Serial.printf("[LORA] Using DevNonce: 0x%04X\n", nextDevNonce);

    int state = node.activateOTAA();

    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
      if (!saveLoRaWANState()) {
        Serial.println("[ERROR] Cannot persist new LoRaWAN session.");
        while (1);
      }

      if (!setPendingOperation(PENDING_NONE)) {
        Serial.println("[ERROR] Cannot clear JoinRequest marker.");
        while (1);
      }

      loraStatus = "OTAA OK";
      Serial.println("[LORA] Join OTAA successful !");
      Serial.println("[LORA] Nonces and session saved.");

    } else if (state == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      if (!saveLoRaWANState()) {
        Serial.println("[ERROR] Cannot persist restored session.");
        while (1);
      }

      if (!setPendingOperation(PENDING_NONE)) {
        Serial.println("[ERROR] Cannot clear JoinRequest marker.");
        while (1);
      }

      loraStatus = "Session restored";

    } else {
      // RadioLib 6.6 increments DevNonce after a JoinRequest is transmitted.
      // Save it even when no JoinAccept is received.
      if (!saveLoRaWANNoncesOnly()) {
        Serial.println("[ERROR] Cannot persist nonces after Join failure.");
        while (1);
      }

      if (!setPendingOperation(PENDING_NONE)) {
        Serial.println("[ERROR] Cannot clear JoinRequest marker.");
        while (1);
      }

      loraStatus = "Err Join:" + String(state);
      Serial.printf("[LORA] Join failure (%d)\n", state);
    }
  }

  // OLED refresh every 2 seconds.
  static uint32_t lastScreen = 0;

  if (millis() - lastScreen > 2000) {
    lastScreen = millis();

    String l1 = "LoRa: " + loraStatus;

    bool gpsFresh =
      gps.location.isValid() &&
      gps.location.age() < 10000;

    String l2 = gpsFresh
      ? "Lat: " + String(gps.location.lat(), 5)
      : "GPS: no fix yet";

    String l3 = gpsFresh
      ? "Lon: " + String(gps.location.lng(), 5)
      : "GPS: waiting for fix";

    uint16_t nextDevNonce = readRadioLibUint16(
      &node.getBufferNonces()[RADIOLIB_LORAWAN_NONCES_DEV_NONCE]
    );

    char nonceText[5];
    snprintf(nonceText, sizeof(nonceText), "%04X", nextDevNonce);

    // Alternate the diagnostic line to preserve the existing OLED information.
    static bool showGpsStats = true;
    showGpsStats = !showGpsStats;

    String l4;

    if (showGpsStats && hasLastTxPosition) {
      uint32_t elapsedMinutes = (millis() - lastSuccessfulTx) / 60000UL;
      char metrics[32];

      snprintf(
        metrics,
        sizeof(metrics),
        "Move:%.0fm Age:%lum",
        lastMovementMeters,
        static_cast<unsigned long>(elapsedMinutes)
      );

      l4 = String(metrics);
    } else if (showGpsStats) {
      l4 = "Move:-- Age:--";
    } else {
      l4 = "S:" + String(gps.satellites.value()) +
        " DN:" + String(nonceText) +
        " TX:" + String(txCount);
    }

    updateOLED(l1, l2, l3, l4);
  }

  // Check the GPS position every minute while the LoRaWAN session is active.
  static uint32_t lastTx = 0;

  if (node.isActivated() &&
      (millis() - lastTx > (txIntervalSec * 1000UL))) {

    lastTx = millis();

    if (gps.location.isValid() && gps.location.age() < 10000) {
      double currentLatitude = gps.location.lat();
      double currentLongitude = gps.location.lng();

      double distanceFromLastTx = hasLastTxPosition
        ? haversineMeters(
            lastTxLatitude,
            lastTxLongitude,
            currentLatitude,
            currentLongitude
          )
        : 0.0;

      if (hasLastTxPosition) {
        lastMovementMeters = distanceFromLastTx;
      }

      bool movementDetected =
        !hasLastTxPosition ||
        distanceFromLastTx >= moveThresholdMeters;

      bool stationaryReportDue =
        !hasLastTxPosition ||
        (millis() - lastSuccessfulTx >=
          static_cast<uint32_t>(stationaryIntervalMinutes) * 60000UL);

      if (movementDetected || stationaryReportDue) {
        Serial.printf(
          "[GPS] Uplink trigger: %s (distance: %.1f m).\n",
          movementDetected ? "movement" : "stationary interval",
          distanceFromLastTx
        );

        // Cayenne LPP encoding: GPS (11 bytes) + battery voltage (4 bytes).
        int32_t lat = gps.location.lat() * 10000;
        int32_t lon = gps.location.lng() * 10000;
        int32_t alt = gps.altitude.meters() * 100;

        // Voltage in hundredths of a volt (412 = 4.12V).
        uint16_t batt = PMU.getBattVoltage() / 10;

        uint8_t payload[15] = {};

        // Channel 1: GPS location (type 0x88).
        payload[0] = 0x01;
        payload[1] = 0x88;
        payload[2] = (lat >> 16) & 0xFF;
        payload[3] = (lat >> 8) & 0xFF;
        payload[4] = lat & 0xFF;
        payload[5] = (lon >> 16) & 0xFF;
        payload[6] = (lon >> 8) & 0xFF;
        payload[7] = lon & 0xFF;
        payload[8] = (alt >> 16) & 0xFF;
        payload[9] = (alt >> 8) & 0xFF;
        payload[10] = alt & 0xFF;

        // Channel 2: 18650 battery voltage (Type 0x02 Analog Input).
        payload[11] = 0x02;
        payload[12] = 0x02;
        payload[13] = (batt >> 8) & 0xFF;
        payload[14] = batt & 0xFF;

        // Mark the uplink as pending before the radio operation.
        if (!setPendingOperation(PENDING_UPLINK)) {
          Serial.println("[ERROR] Cannot persist uplink marker.");
          while (1);
        }

        uint32_t previousFCntUp = node.getFCntUp();
        int state = node.sendReceive(payload, sizeof(payload));
        bool uplinkCounterAdvanced =
          node.getFCntUp() != previousFCntUp;
        bool noDownlink =
          state == RADIOLIB_LORAWAN_NO_DOWNLINK && uplinkCounterAdvanced;

        // sendReceive() may report no downlink even if the uplink was sent.
        // Always persist the session counters after the operation returns.
        if (!saveLoRaWANState()) {
          Serial.println("[ERROR] Cannot persist LoRaWAN session.");
          while (1);
        }

        if (!setPendingOperation(PENDING_NONE)) {
          Serial.println("[ERROR] Cannot clear uplink marker.");
          while (1);
        }

        // Advance the GPS reference if RadioLib consumed an uplink frame
        // counter, even when the receive-window result reports an error.
        if (state >= RADIOLIB_ERR_NONE ||
            state == RADIOLIB_ERR_RX_TIMEOUT ||
            uplinkCounterAdvanced) {
          lastTxLatitude = currentLatitude;
          lastTxLongitude = currentLongitude;
          lastMovementMeters = 0.0;
          lastSuccessfulTx = millis();
          hasLastTxPosition = true;
        }

        if (state >= RADIOLIB_ERR_NONE || noDownlink) {
          txCount++;
          loraStatus = "Connected";

          Serial.printf(
            "[LORA] Cayenne LPP frame sent (Batt: %.2fV).\n",
            batt / 100.0
          );

          if (noDownlink) {
            Serial.println("[LORA] No downlink received (normal).");
          }

        } else {
          loraStatus = "TX err:" + String(state);
          Serial.printf("[LORA] Sent error (%d)\n", state);
        }

      }
    } else {
      Serial.println("[GPS] No recent valid GPS position.");
    }
  }
}
