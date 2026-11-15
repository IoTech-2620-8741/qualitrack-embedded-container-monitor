/**
 * @file ContainerReadingPackage.cpp
 * @brief Implements the readings of the Container Monitor in the format of the Edge API.
 */
#include "ContainerReadingPackage.h"
#include <cmath>
#include <cstring>

namespace {
    /// One decimal is the resolution of the DHT and BH1750 values the node reports.
    float rounded(const float value) {
        return roundf(value * 10.0f) / 10.0f;
    }
}

ContainerReadingPackage::ContainerReadingPackage(const uint32_t measuredAtEpochSeconds, const uint32_t profileVersion)
    : EdgeTelemetryPackage(measuredAtEpochSeconds, profileVersion),
      temperatureInCelsius(NAN),
      humidityInPercentage(NAN),
      luminosityInLux(NAN),
      tag{} {}

std::unique_ptr<ContainerReadingPackage> ContainerReadingPackage::climate(
        const uint32_t measuredAtEpochSeconds, const float temperatureInCelsius, const float humidityInPercentage,
        const float luminosityInLux, const uint32_t profileVersion) {
    std::unique_ptr<ContainerReadingPackage> package(new ContainerReadingPackage(measuredAtEpochSeconds, profileVersion));
    package->temperatureInCelsius = temperatureInCelsius;
    package->humidityInPercentage = humidityInPercentage;
    package->luminosityInLux = luminosityInLux;
    return package;
}

std::unique_ptr<ContainerReadingPackage> ContainerReadingPackage::rfidTag(
        const uint32_t measuredAtEpochSeconds, const char* tag, const uint32_t profileVersion) {
    std::unique_ptr<ContainerReadingPackage> package(new ContainerReadingPackage(measuredAtEpochSeconds, profileVersion));
    if (tag != nullptr) {
        strncpy(package->tag, tag, sizeof(package->tag) - 1);
    }
    return package;
}

const char* ContainerReadingPackage::getEdgeRoute() const {
    return EDGE_ROUTE;
}

void ContainerReadingPackage::serialize(JsonDocument& serializationDestination) const {
    serializeObservationTime(serializationDestination, "measured_at");
    if (!std::isnan(temperatureInCelsius)) serializationDestination["temperature"] = rounded(temperatureInCelsius);
    if (!std::isnan(humidityInPercentage)) serializationDestination["humidity"] = rounded(humidityInPercentage);
    if (!std::isnan(luminosityInLux)) serializationDestination["luminosity"] = rounded(luminosityInLux);
    if (tag[0] != '\0') serializationDestination["rfid_tag"] = static_cast<const char*>(tag);
    serializeProfileVersion(serializationDestination);
}
