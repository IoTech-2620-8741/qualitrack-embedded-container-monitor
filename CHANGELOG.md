# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
