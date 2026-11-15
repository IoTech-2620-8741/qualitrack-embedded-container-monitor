# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Arduino sketch for the ESP32 DevKit v1 on the Modest IoT Nano Framework v1.0.0, with the `ContainerMonitorDevice`
  mediator, which owns the DHT, BH1750 and RC522 sensors and the servo, fan, RGB LED, buzzer and OLED actuators
  (MS06–MS11, US71).
- Container profile with the thresholds of temperature, humidity and luminosity, the classification rule of the cloud
  with a hysteresis margin per metric, the actuation rules, and a default profile while the Edge has none (MS10, MS15).
- Ventilation and simulated cooling on the fan, and the opening of the lock for 10 seconds for the authorized tags,
  each one reported to the Edge with its cause and result (MS10, MS11, MS13).
- Status LED patterns (normal, warning, critical, needs review, access granted, access denied, linking with the Edge)
  and the OLED screen with the data, the lock, the action and the link with the Edge (MS08, US71).
- Acknowledgement of the local alarm with an authorized tag (US74).
- `EdgeGatewayClient` for the container readings, the actuation events and the configuration of the Edge API, with
  the time of the last answer of the Edge (MS12–MS15).
- Wokwi project files, a custom BH1750 chip and the reading of tags typed on the serial monitor, since Wokwi has no
  BH1750 and the circuit has no RFID part.
- `ClimateSensor`, `LockServo` and the frame buffer of `ContainerDisplay`, which replace the `DhtSensor`, `ServoMotor`
  and `OledDisplay` of the framework v1.0.0: in Wokwi the DHT always failed, the servo did not move and the screen
  drew every page on the first two.
- License, changelog and the README with the behavior, the build and the simulation.

### Changed

- The project is an Arduino sketch built with arduino-cli, like the Environment Monitor, instead of a PlatformIO
  project.
