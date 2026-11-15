/**
 * @file ContainerMonitor.ino
 * @brief Embedded Application of the QualiTrack Container Monitor (ESP32 DevKit v1).
 *
 * One Container Monitor watches each container of drug lots: it measures the temperature, humidity
 * (DHT) and luminosity (BH1750) against the environmental profile of the container, starts the
 * ventilation or simulated cooling with the rules of the profile, opens the lock (SG90 servo) only
 * for the RFID tags (RC522) of the authorized staff, shows its state on an OLED, an RGB LED and a
 * buzzer, and sends its readings and actions to the QualiTrack Edge Service of the local station.
 *
 * @par Modest IoT Nano Framework aspects used:
 * - `Device` mediator with the asynchronous engine (sampler and network tasks, empty `loop()`)
 * - `DigitalLightIntensitySensor` and `RfidSensor` sampled by the scheduler, and `ClimateSensor`, a
 *   `Sensor` of the node for the DHT
 * - `RelayActuator`, `RgbLed` and `Buzzer` driven by commands, and `LockServo`, an `Actuator` of the
 *   node for the latch
 * - `WiFiConnectivityDriver` and `TimeSynchronizationDriver`
 * - `GatewayClient` extended by `EdgeGatewayClient` for the QualiTrack Edge API
 * - Scheduler slots of the device itself for the evaluation, the LED, the report and the alarm
 *
 * Circuit of report section 5.6:
 *
 * | Component | ESP32 |
 * |-----------|-------|
 * | OLED SSD1306 and BH1750 (I2C) SDA / SCL | GPIO 21 / 22 |
 * | RC522 (SPI) MOSI / MISO / SCK / SDA / RST | GPIO 23 / 19 / 18 / 5 / 2 |
 * | DHT data | GPIO 15 |
 * | SG90 servo signal | GPIO 13 |
 * | 2N2222 base of the fan (1 kΩ) | GPIO 12 |
 * | RGB LED red / green / blue (common cathode) | GPIO 4 / 16 / 17 |
 * | Buzzer | GPIO 25 |
 */
#include <Arduino.h>
#include <Wire.h>
#include <ModestIoT.h>
#include "ContainerMonitorDevice.h"
#include "EdgeGatewayClient.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

/** @name Pins of the circuit */
//@{
static constexpr int I2C_SDA_PIN = 21;
static constexpr int I2C_SCL_PIN = 22;
static constexpr ContainerMonitorPins PINS = {
    15, // DHT data
    5,  // RC522 SDA (chip select)
    2,  // RC522 RST
    13, // SG90 servo
    12, // 2N2222 base of the fan
    4,  // RGB LED red
    16, // RGB LED green
    17, // RGB LED blue
    25  // Buzzer
};
//@}

/// Speed of the I2C bus shared by the OLED and the BH1750 (a full frame takes about 25 ms).
static constexpr uint32_t I2C_CLOCK_IN_HERTZ = 400000;
/// Serial monitor speed.
static constexpr unsigned long SERIAL_BAUD_RATE = 115200;
/// Wait of the empty loop; the framework tasks do the work.
static constexpr unsigned long MAIN_LOOP_DELAY_IN_MILLISECONDS = 60000;

static WiFiConnectivityDriver* transportDriver = nullptr;
static EdgeGatewayClient* edgeGateway = nullptr;
static ContainerMonitorDevice* containerMonitor = nullptr;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Serial.printf("\n[ContainerMonitor] %s starting\n", DEVICE_ID);

    // Shared I2C bus of the OLED and the BH1750.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_IN_HERTZ);

    transportDriver = new WiFiConnectivityDriver(WIFI_SSID, WIFI_PASSWORD);
    transportDriver->connect(); // The driver keeps reconnecting in the background.

    edgeGateway = new EdgeGatewayClient(*transportDriver, EDGE_BASE_URL, DEVICE_ID, DEVICE_API_KEY,
                                        TIME_ZONE, NTP_SERVER);

    const ContainerMonitorSettings settings = {
        DEVICE_ID,
        CLIMATE_SENSOR_MODEL,
        AUTHORIZED_TAGS,
        sizeof(AUTHORIZED_TAGS) / sizeof(AUTHORIZED_TAGS[0]),
        SERIAL_TAG_INPUT
    };
    containerMonitor = new ContainerMonitorDevice(PINS, settings, *edgeGateway);
}

void loop() {
    // The sampler and network tasks of the framework run the node; the loop only yields.
    vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_IN_MILLISECONDS));
}
