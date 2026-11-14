# QualiTrack Embedded Container Monitor

Embedded Application of the **Container Monitor** of [QualiTrack](https://github.com/IoTech-2620-8741), the IoT
platform of IoTech for the quality control of pharmaceutical laboratories and warehouses. There is one Container Monitor
in each monitored container: an ESP32 that measures temperature, humidity and luminosity, reads the RFID tags of the
people who open the container, evaluates the environmental profile of the container, acts locally (ventilation or
simulated cooling and the closing servo), shows its state on a display, an RGB LED and a buzzer, and sends its readings
and actions to the QualiTrack Edge Service of the facility.

## Responsibilities

| Story | Responsibility |
|-------|----------------|
| MS06 | Read the temperature and humidity of the container (DHT) |
| MS07 | Read the luminosity of the container (BH1750) |
| MS08 | Show the main data of the container on the OLED display |
| MS09 | Read the RFID tag of the person who opens the container (RC522) |
| MS10 | Turn the ventilation or simulated cooling on and off with hysteresis |
| MS11 | Open and close the container with the servo |
| MS12 | Send the telemetry of the container to the Edge API |
| MS13 | Send the actions executed by the container to the Edge API |
| MS14 | Authenticate with the Edge API with its own identifier and API key |
| MS15 | Read its environmental profile (thresholds, actuation rules and version) from the Edge API |
| US71 | Let the operational staff check the data of the container on the device |
| US74 | Let the operational staff acknowledge a local alarm from the device |

## Hardware

Circuit of report section 5.6 (Cirkit Designer).

| Component | Component pin | ESP32 connection | Notes |
|-----------|---------------|------------------|-------|
| ESP32 DevKit v1 | — | — | Wi-Fi 2.4 GHz, USB 5 V power (2 A or more for the servo and the fan) |
| OLED display SSD1306 | VDD / SCK / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | Shared I2C bus |
| BH1750 light sensor | VCC / SCL / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | ADDR not connected (address 0x23) |
| RC522 RFID reader | 3.3V / GND / MOSI / MISO / SCK / SDA / RST | 3V3 / GND / GPIO 23 / 19 / 18 / 5 / 2 | SPI bus |
| DHT sensor | VCC / DATA / GND | 5V / GPIO 15 / GND | Temperature and humidity (DHT11; a DHT22 uses the same wiring) |
| SG90 servo | Signal / VCC / GND | GPIO 13 / 5V / GND | Closing of the container |
| 5 V fan | + / − | 5V / collector of the 2N2222 | Emitter of the 2N2222 to GND |
| 2N2222 transistor | Base | GPIO 12 (1 kΩ) | Switches the fan |
| RGB LED (common cathode) | R / G / B / GND | GPIO 4 / 16 / 17 (200 Ω each) / GND | State signal |
| Buzzer | + / − | GPIO 25 / GND | Alert and access denied sound |

GPIO 2, 12 and 15 are strapping pins of the ESP32: if the RC522 or the transistor cause problems at boot, move them to
free pins such as GPIO 14 or 27 (report section 5.6).

## Contract with the Edge API

The node talks to the QualiTrack Edge Service (`http://<local station>:5000`) over the local network with JSON over
HTTP. It sends its `device_id` (the identifier registered in QualiTrack, for example `CNT-001`) in the body or the path
and the API key the Edge issued for it in the `X-API-Key` header.

| Endpoint | Use |
|----------|-----|
| `POST /api/v1/auth/sign-in` | Check the identifier and the API key of the node |
| `POST /api/v1/monitoring/container-readings` | Send the temperature, humidity, luminosity and RFID tag measured at one moment |
| `POST /api/v1/control/actuation-events` | Send an action the container executed (`VENTILATION_ON`, `VENTILATION_OFF`, `COOLING_ON`, `COOLING_OFF`, `SERVO_OPEN`, `SERVO_CLOSE`) |
| `GET /api/v1/configurations/{deviceId}` | Read the thresholds, the actuation rules and the version of the environmental profile |

Example of a reading and of an action:

```json
{
  "device_id": "CNT-001",
  "measured_at": "2026-11-13T15:00:00Z",
  "temperature": 21.5,
  "humidity": 48.0,
  "luminosity": 120.0,
  "profile_version": 3
}
```

```json
{
  "device_id": "CNT-001",
  "action": "VENTILATION_ON",
  "trigger_metric": "TEMPERATURE",
  "trigger_state": "WARNING",
  "result": "EXECUTED",
  "occurred_at": "2026-11-13T15:00:05Z",
  "profile_version": 3
}
```

The Edge answers `201` when it stores the data, `200` for an identical retry, `400` for invalid data, `401` for a
missing or rejected API key and `409` when a retry changes stored data. See the
[Edge Service](https://github.com/IoTech-2620-8741/qualitrack-edge-service) for the full contract.

## Technologies

C++ with the Arduino Framework for the ESP32, built with PlatformIO.

## Branches and Commits

The repository follows GitFlow: `main` keeps the released versions, `develop` integrates the work, and each change is
made in a `feature/*` branch created from `develop`. Commit messages follow Conventional Commits.

## Related Repositories

- [qualitrack-embedded-environment-monitor](https://github.com/IoTech-2620-8741/qualitrack-embedded-environment-monitor): Embedded Application of the Environment Monitor.
- [qualitrack-edge-service](https://github.com/IoTech-2620-8741/qualitrack-edge-service): Edge Service that receives the readings and actions of the nodes.
- [qualitrack-platform](https://github.com/IoTech-2620-8741/qualitrack-platform): Cloud REST API of QualiTrack.
