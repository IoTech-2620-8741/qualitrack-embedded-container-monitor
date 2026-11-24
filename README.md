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
| Temperature in WARNING | Ventilation: the fan at half speed (`VENTILATION_ON`) |
| Temperature in CRITICAL | Ventilation and cooling, the fan at full speed (`COOLING_ON`); the buzzer every 2 s |
| Authorized RFID tag | The servo opens the container (`SERVO_OPEN`) for 10 seconds |
| Unknown RFID tag | Buzzer and red LED; the container stays closed |
| End of the opening time | The servo closes the container (`SERVO_CLOSE`) |
| The reading is NORMAL again | The action stops (`VENTILATION_OFF`, `COOLING_OFF`) |

The rules come from the profile: each one names a metric, a state and an action, and it keeps acting while the metric
is in that state or a worse one. A metric returns to a better state only once it is inside its range by the hysteresis
margin (0.5 °C, 2 % or 10 lux), so an action does not switch on and off around a limit (MS10). Ventilation and
cooling share the 5 V fan of the container, driven with PWM: ventilation runs it at half speed and cooling at full
speed, and a stopped fan starts at full speed for half a second, since a small fan may not start at half speed. The
node reports every action to the Edge with its cause (metric and state) and its result (`EXECUTED` or `FAILED`); an
opening or closing because of a tag has no cause. A fan action is `EXECUTED` when the PWM output took the new speed:
the fan has no tachometer, so the node cannot tell whether it turns.

An authorized tag presented while the container is CRITICAL also acknowledges the local alarm and silences the buzzer
until the container leaves CRITICAL (US74). The screen shows `ALARM ACK` meanwhile.

### Screen

The 0.96" OLED shows, in this order, the identifier of the container and its overall state; the temperature,
humidity and luminosity, each with its state (`FAULT` for a sensor fault); the active action (`AUTO` when no rule
acts) and the lock (`LOCKED` or `OPEN`); and the link with the Edge (`ONLINE`, `LINKING` or `OFFLINE`). When a
tag is presented, it shows `ACCESS GRANTED` or `ACCESS DENIED` with the UID for 3 seconds.

## Hardware

Circuit of report section 5.6 (Cirkit Designer). Cabinet of 450 × 450 × 600 mm in stainless steel with PU insulation.

| Component | Component pin | ESP32 connection | Notes |
|-----------|---------------|------------------|-------|
| ESP32 DevKit v1 (WROOM-32) | — | — | Wi-Fi 2.4 GHz, USB-C 5 V power (2 A for the servo and the fan) |
| OLED display SSD1306 0.96" | VDD / SCK / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | Shared I2C bus, address 0x3C |
| BH1750 light sensor | VCC / SCL / SDA / GND | 3V3 / GPIO 22 / GPIO 21 / GND | ADDR not connected (address 0x23) |
| RC522 RFID reader | 3.3V / GND / MOSI / MISO / SCK / SDA / RST | 3V3 / GND / GPIO 23 / 19 / 18 / 5 / 2 | SPI bus |
| DHT22 sensor | VCC / DATA / GND | 3V3 / GPIO 15 / GND | Temperature and humidity |
| SG90 servo | Signal / VCC / GND | GPIO 13 / 5V / GND | Latch of the container |
| 5 V fan | + / − | 5V / collector of the 2N2222 | Emitter of the 2N2222 to GND |
| 1N4007 diode | Cathode / anode | 5V / collector of the 2N2222 | Across the fan, against the spike when it stops |
| 2N2222 transistor | Base | GPIO 26 (1 kΩ) | Switches the fan with PWM (1 kHz) |
| RGB LED (common cathode) | R / G / B / GND | GPIO 4 / 16 / 17 (200 Ω each) / GND | Status LED |
| Buzzer | + / − | GPIO 25 / GND | Alarm and access denied sound |

GPIO 2 and 15 are strapping pins of the ESP32: if the RC522 causes problems at boot, move it to a free pin such as
GPIO 27 (report section 5.6) and change the pin constants at the top of `ContainerMonitor.ino`. The fan is on GPIO 26
instead of the GPIO 12 of report section 5.6, because GPIO 12 sets the voltage of the flash at boot and a load on it
can keep the board from starting.

## Firmware

The node is one sketch, `ContainerMonitor/ContainerMonitor.ino`, as in the examples of the framework and like the
Environment Monitor:

| File | Role |
|------|------|
| `ContainerMonitor.ino` | The node: pins, profile, the `ContainerReading` and `ActuationEvent` telemetry packages, the `ClimateSensor` and `OledScreen` that replace two classes of the framework, the `PwmFan`, and the `ContainerMonitor` device |
| `secrets.example.h` | Settings of the node, with the authorized tags; copy it as `secrets.h` (ignored by Git) with the real values |
| `diagram.json`, `libraries.txt` | Circuit and libraries of the Wokwi project (the Arduino IDE ignores them) |
| `../wokwi/bh1750.chip.*` | Custom Wokwi chip of the BH1750, which Wokwi does not include (outside the sketch so the Arduino builder does not compile it) |

The sketch writes what the framework asks an application to write: the telemetry packages with the fields of the Edge
and `ContainerMonitor`, a `Device` that reacts to the events of its sensors and tags, applies the rules, drives the
lock and the alarm, animates the status LED and lays out the screen. The rest comes from the framework:
`DigitalLightIntensitySensor`, `RfidSensor`, `ServoMotor`, `RgbLed`, `Buzzer`, `WiFiConnectivityDriver`,
`TimeSynchronizationDriver` and `HttpGatewayClient`. Two classes of the framework v1.0.0 do not work and cannot be
fixed by a subclass (see the notes below), so the sketch also writes, on the extension points of the framework,
`ClimateSensor` (a `Sensor`, in place of `DhtSensor`) and `OledScreen` (an `Actuator` with the drawing calls and the
update command of `OledDisplay`). The `RelayActuator` of the framework only turns its pin fully on or off, so the
sketch also writes `PwmFan` (an `Actuator` whose `SET_SPEED_COMMAND` carries the speed in percent) for the fan. The
profile read from the Edge on the network task reaches the sampler task through `enqueueCommand()`: one command starts
it, one carries each limit or rule, and the last one applies it.

The node classifies a reading with the rule of the cloud: a value beyond a critical limit is CRITICAL, beyond a normal
limit WARNING, and NORMAL otherwise. The luminosity is the moving average of its last 10 samples, one per second,
against the noise of the BH1750. The node sends the climate every 30 seconds and at once when the state of a metric
changes, each tag it reads, and each action it executes, but only once its clock is synchronized with NTP, because
the Edge needs the time of each observation.

Notes on the framework v1.0.0 (v1.1.0 has the same sources for these classes). The faults of the `DhtSensor` and the
`OledDisplay` are in private, non-virtual methods, so a subclass cannot fix them:

- The `DhtSensor` drives the line high for 40 µs after the start signal and then switches the pin mode while the sensor
  already answers, and waits for the answer against one 120 µs limit for two phases. In Wokwi it never reads the
  DHT22: every attempt fails and the sensor goes to fault after the third one. It also reads each bit with loops that
  have no time limit (a sensor unplugged in the middle of a frame blocks the sampler task), never reports the recovery
  of a fault, and ignores the sign of negative DHT22 temperatures. `ClimateSensor` extends `Sensor` with the pin in
  open-drain mode, a limit per level of the protocol and the frame read in a critical section.
- The `ServoMotor` turns the angle into a share of the whole 20 ms period (pulse = angle / 180 × 20 ms), while an SG90
  needs 0.5–2.4 ms; the test of the framework expects those values (duty 4095 of 8191 at 90°). The node uses it with
  the "angles" that give the pulses of the latch: 5 gives about 0.55 ms (closed, about 4°) and 13 about 1.44 ms (open,
  about 89°).
- The `OledDisplay` keeps the index of its frame buffer in a `uint8_t` (`page * 128 + x` in `setPixel()`), so from the
  third page on every pixel wraps to the first two pages and only the top 16 rows can be drawn. Its `Ssd1306Driver`
  also writes each page in a transaction of 129 bytes, one more than the I2C buffer of the ESP32. `OledScreen` keeps
  the drawing calls of `OledDisplay` (`setText()`, `drawHorizontalLine()`, `drawRectangle()`, `clearFrameBuffer()`)
  and its `UPDATE_DISPLAY_COMMAND`, draws in its own frame buffer with the same font, and sends the setup in one
  transaction and the frame in 16-byte transactions. The sketch starts the I2C bus (GPIO 21 / 22, 400 kHz), and the
  screen is sent only when its text changes.
- The `DigitalLightIntensitySensor` raises an event only when the light crosses 10 lux, so the device reads the latest
  sample every second.
- The `RfidSensor` retries its 50 ms reset on every sample while no reader answers, so the device polls it only when
  the reader answered at boot.
- `HttpGatewayClient::sendTelemetryRecord()` adds the `timestamp` and `sequence` of the framework, which the Edge
  rejects, so the node posts the JSON of each record with `sendRequest()`. The client sends only the `Authorization`
  header, so the API key goes as a Bearer token, which the Edge accepts since v0.4.0 (its ADR-0018).
- `HttpGatewayClient` keeps only 511 bytes of a response, and the configuration of a container (three thresholds and
  its rules) is longer, so the node reads it with the `HTTPClient` of the ESP32 core and the same Bearer token.
- On the ESP32 the `MODEST_LOG_*` macros go to the ESP-IDF log, which prints nothing at the default debug level, so
  the node writes its messages with `Serial`, as the examples of the framework do.

## Build with the Arduino IDE

1. Install the ESP32 core (Espressif, 3.x) and, in the Library Manager, `Modest-IoT-Nano-Framework` 1.0.0, which
   also installs ArduinoJson and MQTT.
2. Copy `ContainerMonitor/secrets.example.h` as `ContainerMonitor/secrets.h` and set the Wi-Fi network, the URL of
   the Edge, the identifier of the node, its API key, the UIDs of the authorized tags, and `SERIAL_TAG_INPUT` to
   `false`.
3. Open `ContainerMonitor/ContainerMonitor.ino`, choose the board "ESP32 Dev Module" and upload it.

## Build with arduino-cli

The same steps from a terminal:

1. Install the ESP32 core and the framework v1.0.0 from the Arduino Library Manager, which also installs the
   libraries it depends on (ArduinoJson and MQTT):

   ```bash
   arduino-cli core install esp32:esp32
   arduino-cli lib install "Modest-IoT-Nano-Framework@1.0.0"
   ```

2. Copy `ContainerMonitor/secrets.example.h` as `ContainerMonitor/secrets.h` and set the Wi-Fi network, the URL of
   the Edge, the identifier of the node, its API key, the UIDs of the authorized tags, and `SERIAL_TAG_INPUT` to
   `false`.

3. Compile (and add `--upload --port <port>` to flash the board):

   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32 ContainerMonitor
   ```

## Simulate in Wokwi

Open the published project: [qualitrack-container-monitor](https://wokwi.com/projects/480802699328400385). When the
sketch changes, update its files there, or build a new project with the steps below.

`libraries.txt` takes the framework v1.0.0 from the Arduino Library Manager, as arduino-cli does.

1. Create a new ESP32 project in Wokwi.
2. Rename `sketch.ino` to `ContainerMonitor.ino`, as in the published project, and paste `ContainerMonitor.ino`,
   `diagram.json` and `libraries.txt` into the files of the same name.
3. Add `secrets.example.h` (without `secrets.h`, so the sketch takes `secrets.example.h`).
4. Add `wokwi/bh1750.chip.json` and `wokwi/bh1750.chip.c` with the same names: Wokwi has no BH1750, so this custom
   chip answers on address 0x23 with the luminosity of its slider.
5. Start the simulation. The first build compiles the whole framework and takes about two minutes.

What to expect (checked in Wokwi with the ESP32 core of the simulator):

- The LED pulses blue while the node looks for the Edge and the screen shows `CNT-001 NORMAL`, the three metrics and
  `AUTO - LOCKED`.
- Move the temperature of the DHT22 to 27 °C: the LED blinks yellow, the blue "Fan" LED lights fully for half a
  second and then at half brightness (half speed), and the screen shows `VENTILATION`. At 32 °C the LED blinks red,
  the "Fan" LED lights fully (full speed), the screen shows `VENT+COOL` and the buzzer sounds every two seconds. Back
  at 24.8 °C the container stays in WARNING (hysteresis); at 24 °C it is NORMAL and the fan stops.
- Tags are typed in the serial monitor (one UID per line), since the Wokwi circuit has no RFID reader: `DE:AD:BE:EF` is
  authorized (green LED for 2 s, `ACCESS GRANTED`, the servo opens for 10 seconds, and in CRITICAL the alarm is
  acknowledged); any other UID is denied (3 red flashes, `ACCESS DENIED`).
- Move the slider of the BH1750 to 300 lux: the luminosity reaches WARNING about 5 seconds later, when the average of
  its last 10 samples passes 200 lux.
- The simulator reaches the internet through `Wokwi-GUEST` but not a local network, so the requests to the Edge fail
  with status -1 unless the Edge has a public address; after 5 minutes without the Edge the LED turns solid red
  (needs review) unless the container is CRITICAL.
- Keep the browser tab in front: a hidden tab slows the simulation to about 1 % of real time.

## Contract with the Edge API

The node talks to the QualiTrack Edge Service (`http://<local station>:5000`) over the local network with JSON over
HTTP. It sends its `device_id` (the identifier registered in QualiTrack, for example `CNT-001`) in the body or the path
and the API key the Edge issued for it as a Bearer token: `Authorization: Bearer <api key>` (the Edge also accepts it
in `X-API-Key`).

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

The node sends `profile_version` once it has a profile of the Edge, and the Edge answers `404` to the configuration
while it has no profile of the cloud for the node. The Edge answers `201` when it stores the data,
`200` for an identical retry, `400` for invalid data, `401` for a missing or rejected API key and `409` when a retry
changes stored data. See the [Edge Service](https://github.com/IoTech-2620-8741/qualitrack-edge-service) for the full
contract.

## Technologies

C++ with the Arduino framework for the ESP32, the Modest IoT Nano Framework v1.0.0 and ArduinoJson 7, built with
the Arduino IDE or arduino-cli and simulated in Wokwi.

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
