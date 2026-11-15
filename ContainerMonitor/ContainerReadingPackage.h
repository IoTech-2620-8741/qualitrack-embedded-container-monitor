/**
 * @file ContainerReadingPackage.h
 * @brief Metrics the Container Monitor measured at one moment (Edge route container-readings, TS56).
 *
 * The Edge receives the temperature, humidity and luminosity of the container and the RFID tag read
 * on it, all optional but at least one of them, with the time of the measurement:
 *
 * @code{.json}
 * { "measured_at": "2026-11-13T15:00:00Z", "temperature": 21.5, "humidity": 48.0, "luminosity": 120.0,
 *   "profile_version": 3 }
 * @endcode
 *
 * `EdgeGatewayClient` adds the `device_id` of the node.
 */
#ifndef CONTAINER_READING_PACKAGE_H
#define CONTAINER_READING_PACKAGE_H

#include <memory>
#include "EdgeTelemetryPackage.h"

/**
 * @brief Temperature, humidity, luminosity or RFID tag of one moment, as the Edge API receives them.
 */
class ContainerReadingPackage : public EdgeTelemetryPackage {
public:
    /// Edge route of the readings of the container monitor.
    static constexpr const char* EDGE_ROUTE = "/api/v1/monitoring/container-readings";
    /// Size of the buffer of an RFID tag ("DE:AD:BE:EF:..." for up to 10 bytes).
    static constexpr size_t TAG_BUFFER_SIZE = 32;

    /**
     * @brief Creates a reading of the climate of the container. A NaN value is left out.
     * @param measuredAtEpochSeconds Unix time of the measurement.
     * @param temperatureInCelsius Temperature in °C, or NaN.
     * @param humidityInPercentage Relative humidity in %, or NaN.
     * @param luminosityInLux Luminosity in lux, or NaN.
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The reading, ready to be enqueued.
     */
    static std::unique_ptr<ContainerReadingPackage> climate(uint32_t measuredAtEpochSeconds,
                                                            float temperatureInCelsius, float humidityInPercentage,
                                                            float luminosityInLux, uint32_t profileVersion);

    /**
     * @brief Creates a reading with the RFID tag presented to the container.
     * @param measuredAtEpochSeconds Unix time of the reading.
     * @param tag UID of the tag, for example "DE:AD:BE:EF".
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The reading, ready to be enqueued.
     */
    static std::unique_ptr<ContainerReadingPackage> rfidTag(uint32_t measuredAtEpochSeconds, const char* tag,
                                                            uint32_t profileVersion);

    ~ContainerReadingPackage() override = default;

    [[nodiscard]] const char* getEdgeRoute() const override;

    /**
     * @brief Writes the fields of the Edge request (without the framework envelope).
     * @param serializationDestination JSON document of the request.
     */
    void serialize(JsonDocument& serializationDestination) const override;

private:
    ContainerReadingPackage(uint32_t measuredAtEpochSeconds, uint32_t profileVersion);

    float temperatureInCelsius;
    float humidityInPercentage;
    float luminosityInLux;
    char tag[TAG_BUFFER_SIZE];
};

#endif // CONTAINER_READING_PACKAGE_H
