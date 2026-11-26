# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- A servo on GPIO 32, the fan gauge, shows the speed of the fan: about 4° when it is stopped, 89° at half speed and
  173° at full speed. In Wokwi it takes the place of the "Fan" LED (MS10, #9).
- Local simulation in Wokwi from VS Code or CLion: `wokwi.toml` with the firmware built by arduino-cli in
  `ContainerMonitor/build` and the custom BH1750 chip, and the compiled chip `wokwi/bh1750.chip.wasm` with its API
  header `wokwi/wokwi-api.h`.
- A "Simulate locally" section in the README, with the build, the license, the simulation and how to rebuild the chip.
- The Wokwi circuit has the MFRC522 RFID reader on the SPI bus (SDA GPIO 5, RST GPIO 2), which the `RfidSensor` of the
  framework reads: the Blue Card has the authorized UID `DE:AD:BE:EF`, the other cards are denied, and UIDs can still
  be typed in the serial monitor. The lock keeps the `ServoMotor` of the framework and the servo is powered from VIN
  (#6, #7).

### Changed

- The node runs on the Modest IoT Nano Framework 1.3.0 instead of 1.0.0; 1.3.0 compiles on the ESP32 cores 3.x and
  4.x. The Edge records write their fields with `write(TelemetryWriter&)` and are sent with a `JsonTelemetryWriter`,
  still without the envelope of the framework; numbers now carry two decimals. ArduinoJson, which the framework no
  longer brings, is included to read the configuration of the Edge, and the rule of the profile is `ProfileRule`,
  since the framework has its own `Rule`. `libraries.txt` pins `Modest-IoT-Nano-Framework@1.3.0`, because Wokwi may
  still resolve the bare name to 1.2.1, which does not compile on its core 3.3.12 (#8).
- `diagram.json` moves from `ContainerMonitor/` to the root of the repository, next to `wokwi.toml`, with the same
  content, so the Wokwi extension and plugin find it. The published Wokwi project is not affected.
- The README fixes the ESP32 core to 3.3.13 and compiles with `--output-dir ContainerMonitor/build`: the
  core 4.0.0 does not compile the framework v1.0.0 (`BLEDevice.h` not found).
- The `RulesEngine` of the framework sends the commands of the fan and of its gauge for each event of the fan (stop,
  ventilation and cooling), as in the SmartFactoryAlarm example of the framework (MS10, #9).
- The fan runs with PWM through the new `PwmFan`, an `Actuator` of the framework, in place of the `RelayActuator`:
  ventilation at half speed and simulated cooling at full speed, and a stopped fan starts at full speed for half a
  second (MS10, #9).
- A fan action is reported as `EXECUTED` when the PWM output took the new speed, instead of reading the level of the
  pin (MS13, #9).
- The fan is on GPIO 26 instead of GPIO 12, a strapping pin that sets the voltage of the flash at boot; the hardware
  table adds the 1N4007 diode across the fan (#9).

## [1.0.0] - 2026-11-15

First stable version of the Embedded Application of the Container Monitor, with the same behavior as 0.1.0. From this
version on, a change that breaks the readings, the actions, the sign-in or the profile read from the Edge API raises
the major version.

## [0.1.0] - 2026-11-15

First version of the Embedded Application of the Container Monitor: the stories MS06–MS15, US71 and US74 of the
QualiTrack Report on the Modest IoT Nano Framework v1.0.0.

### Added

- `ContainerMonitor.ino`, one Arduino sketch for the ESP32 DevKit v1 on the Modest IoT Nano Framework v1.0.0, as in
  the examples of the framework and like the Environment Monitor: the `ContainerMonitor` device owns the DHT, BH1750
  and RC522 sensors and the servo, fan, RGB LED, buzzer and OLED actuators (MS06–MS11, US71). It opens in the Arduino
  IDE with the framework installed from the Library Manager (#1).
- The luminosity is the moving average of its last 10 samples, and the climate is sent every 30 seconds, as report
  section 5.6 describes the node (MS07, MS12) (#1).
- Container profile with the thresholds of temperature, humidity and luminosity, the classification rule of the cloud
  with a hysteresis margin per metric, the actuation rules, and a default profile while the Edge has none (MS10,
  MS15) (#1).
- Ventilation and simulated cooling on the fan, and the opening of the lock for 10 seconds for the authorized tags,
  each one reported to the Edge with its cause and result (MS10, MS11, MS13) (#1).
- Status LED patterns (normal, warning, critical, needs review, access granted, access denied, linking with the Edge)
  and the OLED screen with the data, the lock, the action and the link with the Edge (MS08, US71) (#1).
- Acknowledgement of the local alarm with an authorized tag (US74) (#1).
- The `ContainerReading` and `ActuationEvent` telemetry packages, sent with the framework’s `HttpGatewayClient` and the
  API key as `Authorization: Bearer <key>`, which the Edge accepts since v0.4.0 (MS12–MS14). The configuration of the
  Edge is read with the `HTTPClient` of the ESP32 core, because `HttpGatewayClient` keeps only 511 bytes of a
  response, and reaches the sampler task through `enqueueCommand()` (MS15) (#1).
- Wokwi project files, a custom BH1750 chip and the reading of tags typed on the serial monitor, since Wokwi has no
  BH1750 and the Wokwi circuit has no RFID reader (#1).
- `ClimateSensor` (a `Sensor` of the framework) and `OledScreen` (an `Actuator` of the framework), written in the
  sketch in place of the `DhtSensor` and the `OledDisplay` of the framework v1.0.0: in Wokwi the `DhtSensor` never
  reads the DHT22, and the `OledDisplay` can draw only the top 16 rows of the screen. The `ServoMotor` of the framework
  drives the latch with the angles that give an SG90 its pulses (#1).
- License, changelog and the README with the behavior, the build and the simulation (#1).

### Changed

- The project is an Arduino sketch built with the Arduino IDE or arduino-cli, like the Environment Monitor, instead of
  a PlatformIO project (#1).
