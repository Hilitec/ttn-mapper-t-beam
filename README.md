# The Things Network LoRaWAN Mapper

[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue.svg)](https://www.gnu.org/licenses/gpl-3.0.html)
[![Platform: ESP32](https://img.shields.io/badge/platform-ESP32-orange.svg)](https://www.espressif.com/en/products/socs/esp32)
[![LoRaWAN: EU868](https://img.shields.io/badge/LoRaWAN-EU868-green.svg)](https://www.thethingsnetwork.org/)

An ESP32-based GPS tracker and mapper for **The Things Network (TTN)** and **TTN Mapper**, built around the **LilyGo T-Beam V1.2**.

The device periodically reports its geographical position and battery voltage over LoRaWAN. It uses OTAA activation, persistent credential storage, and movement-aware reporting to provide a practical tracker for TTN Mapper deployments.

**Repository:** <https://github.com/hilitec/ttn-mapper-t-beam>

## Features

- **LoRaWAN OTAA activation** using configurable DevEUI, JoinEUI (AppEUI), and AppKey.
- **Persistent configuration** stored in ESP32 Non-Volatile Storage (NVS).
- **Persistent DevNonce management** to prevent nonce reuse after a reboot.
- **GPS tracking** using TinyGPSPlus.
- **Movement-based reporting** with a configurable distance threshold.
- **Stationary reporting** at a configurable maximum interval.
- **CayenneLPP payload encoding** for TTN integration.
- **Battery voltage monitoring**.
- **OLED status display** with startup splash screen and firmware version.
- **Wi-Fi configuration portal** for initial setup and later configuration updates.
- **Web-based firmware update** through the temporary Wi-Fi portal.
- **USB serial commands** for configuration and diagnostics.
- **Automatic Wi-Fi shutdown** after configuration.
- **Automatic OTAA join retries** when activation fails.

## Hardware

The project is designed for the following hardware:

- **Board:** LilyGo T-Beam V1.2
- **Microcontroller:** ESP32
- **LoRa transceiver:** Semtech SX1276
- **GPS:** Integrated GPS receiver
- **Display:** Integrated OLED display

## How it works

The device checks its GPS position every **60 seconds**.

An uplink is transmitted when either of the following conditions is met:

1. The distance from the last transmitted position reaches the configured movement threshold.
2. The configured stationary reporting interval has elapsed.

This avoids sending an uplink for every GPS check while ensuring that a stationary device continues to report periodically.

### Default GPS reporting settings

| Parameter | Default |
| --- | --- |
| GPS check interval | 60 seconds |
| Movement threshold | 25 metres |
| Stationary reporting interval | 15 minutes |

Both reporting parameters can be changed through the Wi-Fi configuration portal or USB serial commands.

## Initial setup

The device provides a temporary Wi-Fi access point to configure its LoRaWAN credentials and reporting settings.

The configuration portal is enabled for **180 seconds** in either of these situations:

- LoRaWAN credentials have not yet been configured.
- The user button is pressed during device boot.

### Configuration portal

1. Power on the T-Beam.
2. Connect a phone or computer to the Wi-Fi network:

   ```text
   SSID: TBeam-Mapper-Setup
   ```

3. Open the following address in a browser: <http://192.168.4.1>
4. Configure the LoRaWAN credentials and, if required, the GPS reporting parameters.
5. Apply the configuration.

The portal provides the following settings:

- DevEUI
- JoinEUI / AppEUI
- AppKey
- Movement threshold (1 to 10,000 metres)
- Stationary reporting interval (1 to 1,440 minutes)

Wi-Fi is disabled after the configuration has been saved and the device restarts.

> **Note:** The configuration portal can also be opened later using the `PORTAL` USB serial command.

> **Security warning:** The configuration access point is intentionally open and does not require a Wi-Fi password. Only activate the portal when you have physical control of the device. Do not leave it unattended while the portal is active.

## LoRaWAN credentials

The following credentials are required:

| Parameter | Description | Expected format |
| --- | --- | --- |
| `DevEUI` | Device identifier | 16 hexadecimal characters |
| `JoinEUI` (AppEUI) | Application identifier | 16 hexadecimal characters |
| `AppKey` | Application root key | 32 hexadecimal characters |

Credentials can be configured or updated after deployment through either:

- The Wi-Fi configuration portal (`TBeam-Mapper-Setup`).
- USB serial commands.

All credentials are stored persistently in ESP32 NVS and retained across reboots.

> **Security note:** Treat the AppKey as a secret. Do not publish real device credentials in source code, screenshots, serial logs, or issue reports.

> **Format note:** The firmware expects the credential lengths shown above. The current implementation checks the lengths but does not strictly validate that every character is hexadecimal.

## USB serial commands

Connect to the device over USB using a serial terminal configured for **115200 baud**.

Commands are entered as plain text, followed by a newline.

| Command | Description |
| --- | --- |
| `INFO` | Display configured identifiers and GPS reporting settings. |
| `SET_DEVEUI=<value>` | Set the DevEUI (16 hexadecimal characters). |
| `SET_APPEUI=<value>` | Set the JoinEUI / AppEUI (16 hexadecimal characters). |
| `SET_APPKEY=<value>` | Set the AppKey (32 hexadecimal characters). |
| `SET_MOVE_THRESHOLD=<meters>` | Set the minimum movement distance (1 to 10,000 metres). |
| `SET_STATIONARY_INTERVAL=<minutes>` | Set the maximum interval between stationary reports (1 to 1,440 minutes). |
| `PORTAL` | Open the Wi-Fi configuration portal. |
| `REBOOT` | Reboot the device. |

### Examples

Set the movement threshold to 50 metres:

```text
SET_MOVE_THRESHOLD=50
```

Set the stationary reporting interval to 15 minutes:

```text
SET_STATIONARY_INTERVAL=15
```

Update the DevEUI:

```text
SET_DEVEUI=0123456789ABCDEF
```

> **Important:** Updating a LoRaWAN credential causes the device to reboot so that the new configuration can be applied.

## Firmware update

The firmware can be updated without a USB connection through the temporary Wi-Fi configuration portal.

1. Open the configuration portal using the `PORTAL` command or by pressing the user button during boot.
2. Connect to the `TBeam-Mapper-Setup` Wi-Fi access point.
3. Open <http://192.168.4.1>.
4. Select **Firmware Update**.
5. Choose the application firmware file named `firmware.bin`.
6. Start the upload and wait for the device to restart.

The firmware update page displays the current firmware version and the upload progress.

**Important:**

- Use the application firmware image (`firmware.bin`) for this update procedure.
- Do not use the merged initial-flash image for the web-based update.
- Do not disconnect power while the firmware is being written.
- The access point is open. Perform firmware updates only when physical access to the device is controlled.

### Initial firmware flash

For an initial installation, use the merged firmware image (`firmware-merged.bin`) with an ESP32 flashing tool.

An online flashing tool is available at:

<https://esptool.spacehuhn.com/>

Make sure to use the appropriate merged image and flash configuration for the T-Beam V1.2.

## The Things Network configuration

Create an application and register the T-Beam as an end device in the TTN Console.

### LoRaWAN settings

| Parameter | Value |
| --- | --- |
| Activation | OTAA |
| LoRaWAN version | LoRaWAN Specification 1.0.4 |
| Regional Parameters | RP002 Regional Parameters 1.0.4 |
| Frequency plan | Europe 863–870 MHz (EU868) |
| Device hardware | LilyGo T-Beam V1.2 / SX1276 |

Configure the device identifiers and AppKey in the device and in the corresponding TTN end-device registration. Make sure the values match exactly.

### Payload formatter

In the TTN Console, configure the uplink payload formatter as:

```text
CayenneLPP
```

This allows TTN to decode the supported CayenneLPP fields, including the GPS position and battery voltage when present in the uplink payload.

### TTN Mapper integration

To forward uplinks to TTN Mapper, configure a TTN webhook integration:

| Setting | Value |
| --- | --- |
| Integration | TTN Mapper |
| Forward format | JSON |
| Base URL | `https://integrations.ttnmapper.org/tts/v3` |

Follow the TTN Mapper integration instructions when creating the webhook in the TTN Console.

## Dependencies

The project uses the following libraries and development framework:

- [Arduino framework for ESP32](https://github.com/espressif/arduino-esp32)
- [RadioLib](https://github.com/jgromes/RadioLib)
- [TinyGPSPlus](https://github.com/mikalhart/TinyGPSPlus)
- [XPowersLib](https://github.com/lewisxhe/XPowersLib)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [U8g2](https://github.com/olikraus/u8g2)

Install the required libraries in your Arduino or PlatformIO development environment before building the firmware.

## License

This project is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License v3.0 or later**.

This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the [GNU General Public License](https://www.gnu.org/licenses/gpl-3.0.html) for details.

## Project information

| | |
| --- | --- |
| **Project** | The Things Network LoRaWAN Mapper |
| **Version** | 1.2.0 |
| **Release date** | 2026-11-15 |
| **Copyright** | © 2026 Hilitec |
| **Author** | Andre Liechti |
| **License** | GNU GPL v3.0 or later |
| **SPDX identifier** | `GPL-3.0-or-later` |
| **Repository** | [hilitec/ttn-mapper-t-beam](https://github.com/hilitec/ttn-mapper-t-beam) |

---

*Developed by Hilitec.*