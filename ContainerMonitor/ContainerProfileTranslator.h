/**
 * @file ContainerProfileTranslator.h
 * @brief Translates the configuration the Edge hands the node into its container profile.
 *
 * `GET /api/v1/configurations/{deviceId}` answers with the profile of the cloud the node must apply:
 *
 * @code{.json}
 * {
 *   "device_id": "CNT-001", "device_type": "CONTAINER_MONITOR", "profile_version": 3,
 *   "thresholds": [{ "metric": "TEMPERATURE", "unit": "°C", "normal_min": 15, "normal_max": 25,
 *                    "critical_min": 10, "critical_max": 30 }],
 *   "actuation_rules": [{ "metric": "TEMPERATURE", "state": "WARNING", "action": "VENTILATION_ON" }],
 *   "updated_at": "2026-11-13T15:00:00Z", "synced_at": "2026-11-13T15:05:00Z"
 * }
 * @endcode
 *
 * The thresholds of TEMPERATURE, HUMIDITY and LUMINOSITY concern this node; RFID_TAG has none.
 */
#ifndef CONTAINER_PROFILE_TRANSLATOR_H
#define CONTAINER_PROFILE_TRANSLATOR_H

#include <ArduinoJson.h>
#include "ContainerProfile.h"

/**
 * @brief Anti-corruption translation from the Edge configuration resource to `ContainerProfile`.
 */
class ContainerProfileTranslator {
public:
    /// Device type of the Container Monitor in the Edge and in the cloud.
    static constexpr const char* CONTAINER_MONITOR_DEVICE_TYPE = "CONTAINER_MONITOR";

    /**
     * @brief Reads the container profile of a configuration of the Edge.
     * @param configuration Body of the configuration resource.
     * @param destination Receives the profile when the configuration is valid.
     * @return False when the configuration is not of a container monitor, has no threshold of the
     *         container metrics, or breaks the rules of the cloud; the node then keeps its profile.
     */
    static bool fromEdgeConfiguration(JsonObjectConst configuration, ContainerProfile& destination);

    /**
     * @brief Name of the action that starts or stops a response, as the Edge spells it.
     * @param action The response.
     * @param activation True for the action that starts it, false for the one that stops it.
     * @return For example "VENTILATION_ON" or "SERVO_CLOSE".
     */
    static const char* actionNameOf(ResponseAction action, bool activation);

private:
    static bool metricOf(const char* name, ContainerMetric& metric);
    static bool stateOf(const char* name, EnvironmentalState& state);
    static bool activationOf(const char* name, ResponseAction& action);
    /** @return The limit, or NaN when it is null or missing (open side). */
    static float limitOf(JsonObjectConst threshold, const char* key);
};

#endif // CONTAINER_PROFILE_TRANSLATOR_H
