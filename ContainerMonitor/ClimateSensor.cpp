/**
 * @file ClimateSensor.cpp
 * @brief Implements the single-wire protocol of the DHT11 and DHT22.
 */
#include "ClimateSensor.h"
#include <cmath>

namespace {
    /// Start signal: the host holds the line low (at least 18 ms for the DHT11, 1 ms for the DHT22).
    constexpr uint32_t DHT11_START_LOW_IN_MICROSECONDS = 20000;
    constexpr uint32_t DHT22_START_LOW_IN_MICROSECONDS = 1100;
    /// Limit of each phase of the answer (about 80 µs low and 80 µs high).
    constexpr uint32_t RESPONSE_TIMEOUT_IN_MICROSECONDS = 250;
    /// Limit of each level of a bit (50 µs low, then 26-28 µs high for 0 or 70 µs high for 1).
    constexpr uint32_t BIT_TIMEOUT_IN_MICROSECONDS = 120;
    constexpr uint32_t BIT_ONE_MINIMUM_HIGH_IN_MICROSECONDS = 45;
    constexpr uint32_t DHT11_MINIMUM_INTERVAL_IN_MILLISECONDS = 1000;
    constexpr uint32_t DHT22_MINIMUM_INTERVAL_IN_MILLISECONDS = 2000;
    /// Open drain with the internal pull-up: writing HIGH releases the line, so no pinMode() is needed.
    constexpr uint8_t OPEN_DRAIN_WITH_PULLUP = OUTPUT_OPEN_DRAIN | PULLUP;
}

ClimateSensor::ClimateSensor(const int signalGpioPin, const uint8_t model, EventHandler* parentHandler)
    : Sensor(signalGpioPin, parentHandler),
      model(model),
      temperatureInCelsius(NAN),
      humidityInPercentage(NAN) {
    pinMode(pin, OPEN_DRAIN_WITH_PULLUP);
    digitalWrite(pin, HIGH);
}

float ClimateSensor::getTemperatureInCelsius() const {
    return temperatureInCelsius;
}

float ClimateSensor::getRelativeHumidityInPercentage() const {
    return humidityInPercentage;
}

uint32_t ClimateSensor::getDefaultSamplingIntervalInMilliseconds() const {
    return DEFAULT_SAMPLING_INTERVAL_IN_MILLISECONDS;
}

uint32_t ClimateSensor::getMinimalSamplingIntervalInMilliseconds() const {
    return model == MODEL_DHT11 ? DHT11_MINIMUM_INTERVAL_IN_MILLISECONDS : DHT22_MINIMUM_INTERVAL_IN_MILLISECONDS;
}

void ClimateSensor::processEvent(Event& event) {
    if (event.identifier != MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER) return;
    uint8_t data[DATA_BYTE_COUNT] = {};
    const bool checksumValid = readFrame(data)
        && static_cast<uint8_t>(data[0] + data[1] + data[2] + data[3]) == data[4];
    if (!checksumValid) {
        reportReadFailure(event);
        if (event.identifier != SENSOR_FAULT_EVENT_IDENTIFIER) event.identifier = PROPAGATION_SUPPRESSION_IDENTIFIER;
        return;
    }
    float humidity;
    float temperature;
    if (model == MODEL_DHT11) {
        humidity = data[0] + data[1] * 0.1f;
        temperature = data[2] + (data[3] & 0x0F) * 0.1f;
        if (data[3] & 0x80) temperature = -temperature;
    } else {
        humidity = static_cast<float>((data[0] << 8) | data[1]) / 10.0f;
        temperature = static_cast<float>(((data[2] & 0x7F) << 8) | data[3]) / 10.0f;
        if (data[2] & 0x80) temperature = -temperature;
    }
    reportReadSuccess(event);
    const bool changed = temperature != temperatureInCelsius || humidity != humidityInPercentage;
    temperatureInCelsius = temperature;
    humidityInPercentage = humidity;
    if (event.identifier == SENSOR_RECOVERED_EVENT_IDENTIFIER) return; // The recovery is propagated.
    event = changed ? Event(DATA_READ_EVENT_IDENTIFIER, pin) : Event(PROPAGATION_SUPPRESSION_IDENTIFIER);
}

bool ClimateSensor::waitForLevel(const int level, const uint32_t timeoutInMicroseconds,
                                 uint32_t& elapsedInMicroseconds) const {
    const uint32_t startedAt = micros();
    while (digitalRead(pin) != level) {
        if (micros() - startedAt > timeoutInMicroseconds) return false;
    }
    elapsedInMicroseconds = micros() - startedAt;
    return true;
}

bool ClimateSensor::readFrame(uint8_t* data) {
    digitalWrite(pin, LOW);
    delayMicroseconds(model == MODEL_DHT11 ? DHT11_START_LOW_IN_MICROSECONDS : DHT22_START_LOW_IN_MICROSECONDS);

    bool complete = false;
    uint32_t elapsed = 0;
    portENTER_CRITICAL(&timingLock);
    digitalWrite(pin, HIGH);
    // Answer of the sensor: low, high, then the low that starts the first bit.
    if (waitForLevel(LOW, RESPONSE_TIMEOUT_IN_MICROSECONDS, elapsed)
        && waitForLevel(HIGH, RESPONSE_TIMEOUT_IN_MICROSECONDS, elapsed)
        && waitForLevel(LOW, RESPONSE_TIMEOUT_IN_MICROSECONDS, elapsed)) {
        complete = true;
        for (uint8_t bit = 0; bit < DATA_BYTE_COUNT * 8 && complete; ++bit) {
            uint32_t highTime = 0;
            complete = waitForLevel(HIGH, BIT_TIMEOUT_IN_MICROSECONDS, elapsed)
                       && waitForLevel(LOW, BIT_TIMEOUT_IN_MICROSECONDS, highTime);
            data[bit / 8] = static_cast<uint8_t>((data[bit / 8] << 1)
                                                 | (highTime >= BIT_ONE_MINIMUM_HIGH_IN_MICROSECONDS ? 1 : 0));
        }
    }
    portEXIT_CRITICAL(&timingLock);
    digitalWrite(pin, HIGH);
    return complete;
}
