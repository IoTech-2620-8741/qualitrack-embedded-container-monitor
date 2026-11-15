/**
 * @file ClimateSensor.h
 * @brief Temperature and humidity of the container from a DHT11 or DHT22 (MS06).
 *
 * The `DhtSensor` of the framework v1.0.0 measures the answer of the sensor with one 120 µs limit
 * for the whole handshake and switches the pin mode in the middle of it, so it misses the answer when
 * `pinMode` is slow (in Wokwi it never reads). This sensor extends the `Sensor` extension point of the
 * framework instead: the pin stays in open-drain mode with its pull-up, each phase of the protocol has
 * its own time limit, and the 5 ms frame is read inside a critical section.
 *
 * It raises `DATA_READ` when a value changes, and the fault and recovery events of `Sensor`.
 */
#ifndef CLIMATE_SENSOR_H
#define CLIMATE_SENSOR_H

#include <ModestIoT.h>

/**
 * @brief DHT11 / DHT22 sensor of the container, read on the sampler task.
 */
class ClimateSensor : public Sensor {
public:
    /// DHT11 model (integer values, 1 s between reads).
    static constexpr uint8_t MODEL_DHT11 = 11;
    /// DHT22 model (tenths, 2 s between reads).
    static constexpr uint8_t MODEL_DHT22 = 22;
    /// Default interval between two reads.
    static constexpr uint32_t DEFAULT_SAMPLING_INTERVAL_IN_MILLISECONDS = 2000;

    /**
     * @param signalGpioPin GPIO of the data line.
     * @param model MODEL_DHT11 or MODEL_DHT22.
     * @param parentHandler Receives the events of the sensor.
     */
    ClimateSensor(int signalGpioPin, uint8_t model, EventHandler* parentHandler = nullptr);

    /** @return Temperature in °C of the last read, or NaN before the first one. */
    [[nodiscard]] float getTemperatureInCelsius() const;

    /** @return Relative humidity in % of the last read, or NaN before the first one. */
    [[nodiscard]] float getRelativeHumidityInPercentage() const;

protected:
    void processEvent(Event& event) override;
    [[nodiscard]] uint32_t getDefaultSamplingIntervalInMilliseconds() const override;
    [[nodiscard]] uint32_t getMinimalSamplingIntervalInMilliseconds() const override;

private:
    static constexpr int PROPAGATION_SUPPRESSION_IDENTIFIER = -1;
    static constexpr uint8_t DATA_BYTE_COUNT = 5;

    uint8_t model;
    float temperatureInCelsius;
    float humidityInPercentage;
    portMUX_TYPE timingLock = portMUX_INITIALIZER_UNLOCKED;

    /** @return True when the 40 bits of the sensor were read (the checksum is checked by the caller). */
    bool readFrame(uint8_t* data);

    /**
     * @brief Waits until the line reaches a level.
     * @param level HIGH or LOW.
     * @param timeoutInMicroseconds Time limit of this phase.
     * @param elapsedInMicroseconds Receives the time waited.
     * @return False when the limit passed.
     */
    bool waitForLevel(int level, uint32_t timeoutInMicroseconds, uint32_t& elapsedInMicroseconds) const;
};

#endif // CLIMATE_SENSOR_H
