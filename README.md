# QualiTrack Embedded Container Monitor

Embedded Application of the **Container Monitor** of [QualiTrack](https://github.com/IoTech-2620-8741), the IoT
platform of IoTech for the quality control of pharmaceutical laboratories and warehouses. There is one Container Monitor
in each monitored container of drug lots: an ESP32 that measures temperature, humidity and luminosity, reads the RFID
tags of the people who open the container, evaluates the environmental profile of the container, acts locally
(ventilation or simulated cooling and the closing servo), shows its state on a display, an RGB LED and a buzzer, and
sends its readings and actions to the QualiTrack Edge Service of the facility.

The firmware is an Arduino sketch built on the [Modest IoT Nano Framework](https://github.com/modest-iot/modest-iot-nano-framework)
v1.0.0, and it runs on the ESP32 DevKit v1 and in the [Wokwi](https://wokwi.com) simulator.

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

The local reaction does not depend on the network: with no Edge or no profile yet, the node evaluates the readings
with its default profile (15–25 °C normal and 10–30 °C critical, 30–60 % normal and 20–70 % critical humidity, up to
200 lux normal and 500 lux critical, ventilation on temperature WARNING and cooling on temperature CRITICAL).

## Behavior

### Status LED

The RGB LED on the top frame shows the status of the node; the OLED repeats it as text.

| Status | Color | Pattern | Meaning |
|--------|-------|---------|---------|
| Normal operation | Green | Slow pulse | Temperature, humidity and luminosity inside the normal range |
| Warning | Yellow | Slow blink | A metric outside the normal range; the rules of WARNING act (ventilation) |
| Critical alert | Red | Fast blink | A metric outside the critical range; the rules act and the buzzer sounds |
| Needs review | Red | Solid | No answer of the Edge for 5 minutes, or a sensor fault: check the network or the power |
| Access granted | Green | Solid for 2 s | Authorized tag: the servo opens the container |
| Access denied | Red | 3 flashes | Unknown tag: the servo stays closed and the buzzer sounds |
| Linking with the Edge | Blue | Soft pulse | The Edge has not accepted the node yet |

A critical alert has priority over the review signal, and the review signal over a warning.

### Automatic actions

| Event | Action |
|-------|--------|
| Temperature in WARNING | Ventilation (`VENTILATION_ON`) |
| Temperature in CRITICAL | Ventilation and cooling on the fan (`COOLING_ON`) and the buzzer every 2 seconds |
| Authorized RFID tag | The servo opens the container (`SERVO_OPEN`) for 10 seconds |
| Unknown RFID tag | Buzzer and red LED; the container stays closed |
| End of the opening time | The servo closes the container (`SERVO_CLOSE`) |
| The reading is NORMAL again | The action stops (`VENTILATION_OFF`, `COOLING_OFF`) |

The rules come from the profile: each one names a metric, a state and an action, and it keeps acting while the metric
is in that state or a worse one. A metric returns to a better state only once it is inside its range by the hysteresis
margin (0.5 °C, 2 % or 10 lux), so an action does not switch on and off around a limit (MS10). Ventilation and
cooling share the 5 V fan of the container. The node reports every action to the Edge with its cause (metric and
state) and its result (`EXECUTED` or `FAILED`); an opening or closing because of a tag has no cause.

An authorized tag presented while the container is CRITICAL also acknowledges the local alarm and silences the buzzer
until the container leaves CRITICAL (US74). The screen shows `ALARM ACK` meanwhile.

### Screen

The 0.96" OLED shows, in this order, the identifier of the container and its overall state; the temperature,
humidity and luminosity, each with its state (`FAULT` for a sensor fault); the active action (`AUTO` when no rule
acts) and the lock (`LOCKED` or `OPEN`); and the link with the Edge (`ONLINE`, `LINKING` or `NO EDGE`). When a
tag is presented, it shows `ACCESS GRANTED` or `ACCESS DENIED` with the UID for 3 seconds.

## Hardware

Circuit of report section 5.6 (Cirkit Designer). Cabinet of 450 × 450 × 600 mm in stainless steel with PU insulation.

| Component | Component pin | ESP32 connection | Notes |
|-----------|---------------|------------------|-------|
| ESP32 DevKit v1 (WROOM-32) | — | — | Wi-Fi 2.4 GHz, USB-C 5 V power (2 A for the servo and the fan) |
| OLED display SSD1306 0.96" | VDD / SCK / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | Shared I2C bus, address 0x3C |
| BH1750 light sensor | VCC / SCL / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | ADDR not connected (address 0x23) |
| RC522 RFID reader | 3.3V / GND / MOSI / MISO / SCK / SDA / RST | 3V3 / GND / GPIO 23 / 19 / 18 / 5 / 2 | SPI bus |
| DHT11 sensor | VCC / DATA / GND | 5V / GPIO 15 / GND | Temperature and humidity (a DHT22 uses the same wiring) |
| SG90 servo | Signal / VCC / GND | GPIO 13 / 5V / GND | Latch of the container |
| 5 V fan | + / − | 5V / collector of the 2N2222 | Emitter of the 2N2222 to GND |
| 2N2222 transistor | Base | GPIO 12 (1 kΩ) | Switches the fan |
| RGB LED (common cathode) | R / G / B / GND | GPIO 4 / 16 / 17 (220 Ω each) / GND | Status LED |
| Buzzer | + / − | GPIO 25 / GND | Alarm and access denied sound |

GPIO 2, 12 and 15 are strapping pins of the ESP32: if the RC522 or the transistor cause problems at boot, move them to
free pins such as GPIO 14 or 27 (report section 5.6) and change `PINS` in `ContainerMonitor.ino`.

## Firmware

| File | Role |
|------|------|
| `ContainerMonitor.ino` | Pins of the circuit; starts the I2C bus and creates the Wi-Fi driver, the Edge gateway and the device |
| `ContainerMonitorDevice` | `Device` mediator: owns the sensors and actuators, applies the rules, the lock and the alarm |
| `ContainerProfile` | Thresholds of each metric, actuation rules and version; classifies like the cloud, with hysteresis |
| `ContainerProfileTranslator` | Reads the thresholds and rules of the configuration the Edge returns |
| `ClimateSensor` | `Sensor` of the DHT11 / DHT22 with a time limit per phase of the protocol |
| `LockServo` | `Actuator` of the SG90 latch with the 0.5–2.4 ms pulses of a servo |
| `StatusIndicator` | Patterns of the status LED |
| `ContainerDisplay` | Screen of the OLED, drawn in its own frame buffer and sent to the SSD1306 |
| `EdgeTelemetryPackage`, `ContainerReadingPackage`, `ActuationEventPackage` | Telemetry packages with the fields of the Edge API (times in UTC) |
| `EdgeGatewayClient` | `GatewayClient` for the Edge API: `X-API-Key` header, NTP clock, sign-in check, profile refresh and last contact |
| `NodeLog.h` | Messages of the node on the serial monitor |
| `secrets.example.h` | Settings of the node, with the authorized tags; copy it as `secrets.h` (ignored by Git) with the real values |
| `diagram.json`, `libraries.txt` | Circuit and libraries of the Wokwi project |
| `../wokwi/bh1750.chip.*` | Custom Wokwi chip of the BH1750, which Wokwi does not include (outside the sketch so arduino-cli does not compile it) |

The node classifies a reading with the rule of the cloud: a value beyond a critical limit is CRITICAL, beyond a normal
limit WARNING, and NORMAL otherwise. It sends the climate every minute and at once when the state of a metric
changes, each tag it reads, and each action it executes, but only once its clock is synchronized with NTP, because
the Edge needs the time of each observation.

Notes on the framework v1.0.0:

- The `DhtSensor` measures the whole answer of the sensor against one 120 µs limit and changes the pin mode in the
  middle of it, so it misses the answer (in Wokwi it always reported a fault). `ClimateSensor` extends `Sensor` with
  the pin in open-drain mode and a limit per phase.
- The `ServoMotor` turns the angle into a share of the whole 20 ms period (90° gives a 10 ms pulse), so a servo does not
  move. `LockServo` extends `Actuator` and sends 0.5–2.4 ms pulses.
- The `OledDisplay` sends each argument of a command in its own I2C transaction and writes pages with page-addressing
  commands while the panel is in horizontal mode; in Wokwi every page landed on the first two. `ContainerDisplay`
  draws in its own frame buffer and sends the setup in one transaction and the frame in 16-byte transactions. The
  sketch starts the I2C bus (GPIO 21 / 22, 400 kHz), and the screen is sent only when its text changes.
- The `DigitalLightIntensitySensor` raises an event only when the light crosses 10 lux, so the device reads the latest
  sample every second.
- The `RfidSensor` retries its 50 ms reset on every sample while no reader answers, so the device polls it only when
  the reader answered at boot.
- On the ESP32 the `MODEST_LOG_*` macros go to the ESP-IDF log, which prints nothing at the default debug level, so
  the node writes its messages to `Serial` (`NodeLog.h`). Define `NODE_LOG_DEBUG` to also print the requests sent.

## Build with arduino-cli

1. Install the ESP32 core and the framework v1.0.0 from the Arduino Library Manager, which also installs the
   libraries it depends on (ArduinoJson and MQTT):

   ```bash
   arduino-cli core install esp32:esp32
   arduino-cli lib install "Modest-IoT-Nano-Framework@1.0.0"
   ```

2. Copy `ContainerMonitor/secrets.example.h` as `ContainerMonitor/secrets.h` and set the Wi-Fi network, the URL of
   the Edge, the identifier of the node, its API key, the UIDs of the authorized tags, `CLIMATE_SENSOR_MODEL` to
   `ClimateSensor::MODEL_DHT11` and `SERIAL_TAG_INPUT` to `false`.

3. Compile (and add `--upload --port <port>` to flash the board):

   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 ContainerMonitor
   ```

## Simulate in Wokwi

`libraries.txt` takes the framework v1.0.0 from the Arduino Library Manager, as arduino-cli does.

1. Create a new ESP32 project in Wokwi.
2. Paste `ContainerMonitor.ino` into `sketch.ino`, and `diagram.json` and `libraries.txt` into the files of the same
   name.
3. Add the other `.h` and `.cpp` files of `ContainerMonitor/` (without `secrets.h`, so the sketch takes
   `secrets.example.h`).
4. Add `wokwi/bh1750.chip.json` and `wokwi/bh1750.chip.c` with the same names: Wokwi has no BH1750, so this custom
   chip answers on address 0x23 with the luminosity of its slider.
5. Start the simulation. The first build compiles the whole framework and takes about two minutes.

What to expect (checked in Wokwi with the ESP32 core of the simulator):

- The LED pulses blue while the node looks for the Edge and the screen shows `CNT-001 NORMAL`, the three metrics and
  `AUTO - LOCKED`.
- Move the temperature of the DHT22 to 27 °C: the LED blinks yellow, the blue "Fan" LED turns on and the screen
  shows `VENTILATION`. At 32 °C the LED blinks red, the screen shows `VENT+COOL` and the buzzer sounds every two
  seconds. Back at 24.8 °C the container stays in WARNING (hysteresis); at 24 °C it is NORMAL and the fan stops.
- Tags are typed in the serial monitor (one UID per line), since the circuit has no RFID part: `DE:AD:BE:EF` is
  authorized (green LED for 2 s, `ACCESS GRANTED`, the servo opens for 10 seconds, and in CRITICAL the alarm is
  acknowledged); any other UID is denied (3 red flashes, `ACCESS DENIED`).
- Move the slider of the BH1750 above 200 lux for a luminosity WARNING.
- The simulator reaches the internet through `Wokwi-GUEST` but not a local network, so the requests to the Edge fail
  with status -1 unless the Edge has a public address; after 5 minutes without the Edge the LED turns solid red
  (needs review) unless the container is CRITICAL.
- Keep the browser tab in front: a hidden tab slows the simulation to about 1 % of real time.

## Contract with the Edge API

The node talks to the QualiTrack Edge Service (`http://<local station>:5000`) over the local network with JSON over
HTTP. It sends its `device_id` (the identifier registered in QualiTrack, for example `CNT-001`) in the body or the path
and the API key the Edge issued for it in the `X-API-Key` header.

| Endpoint | Use |
|----------|-----|
| `GET /api/v1/health` | Check that the Edge is reachable (no API key) |
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

The node sends `profile_version` once it has a profile of the Edge. The Edge answers `201` when it stores the data,
`200` for an identical retry, `400` for invalid data, `401` for a missing or rejected API key and `409` when a retry
changes stored data. See the [Edge Service](https://github.com/IoTech-2620-8741/qualitrack-edge-service) for the full
contract.

## Technologies

C++ with the Arduino framework for the ESP32, the Modest IoT Nano Framework v1.0.0 and ArduinoJson 7, built with
arduino-cli and simulated in Wokwi.

## Branches and Commits

The repository follows GitFlow: `main` keeps the released versions, `develop` integrates the work, and each change is
made in a `feature/*` branch created from `develop`. Commit messages follow Conventional Commits.

## Changelog and License

- [CHANGELOG.md](CHANGELOG.md) lists the changes of every version, following Keep a Changelog.
- QualiTrack is proprietary software of IoTech; see [LICENSE.md](LICENSE.md). The Modest IoT Nano Framework and
  ArduinoJson keep their own licenses (Apache-2.0 and MIT).

## Related Repositories

- [qualitrack-embedded-environment-monitor](https://github.com/IoTech-2620-8741/qualitrack-embedded-environment-monitor): Embedded Application of the Environment Monitor.
- [qualitrack-edge-service](https://github.com/IoTech-2620-8741/qualitrack-edge-service): Edge Service that receives the readings and actions of the nodes.
- [qualitrack-platform](https://github.com/IoTech-2620-8741/qualitrack-platform): Cloud REST API of QualiTrack.
- [modest-iot-nano-framework](https://github.com/modest-iot/modest-iot-nano-framework): framework of the firmware.
