/**
 * @file ContainerProfileTranslator.cpp
 * @brief Implements the translation of the Edge configuration into the container profile.
 */
#include "ContainerProfileTranslator.h"
#include <cmath>
#include <cstring>

bool ContainerProfileTranslator::fromEdgeConfiguration(const JsonObjectConst configuration,
                                                       ContainerProfile& destination) {
    const char* deviceType = configuration["device_type"];
    if (deviceType == nullptr || strcmp(deviceType, CONTAINER_MONITOR_DEVICE_TYPE) != 0) return false;

    const JsonVariantConst version = configuration["profile_version"];
    if (!version.is<uint32_t>() || version.as<uint32_t>() == ContainerProfile::DEFAULT_PROFILE_VERSION) return false;

    ContainerProfile profile(version.as<uint32_t>());
    bool hasThreshold = false;
    for (const JsonObjectConst threshold : configuration["thresholds"].as<JsonArrayConst>()) {
        ContainerMetric metric;
        if (!metricOf(threshold["metric"].as<const char*>(), metric)) continue; // RFID_TAG or a metric of another node.
        const MetricThreshold limits(limitOf(threshold, "normal_min"), limitOf(threshold, "normal_max"),
                                     limitOf(threshold, "critical_min"), limitOf(threshold, "critical_max"));
        if (!limits.isValid()) return false;
        profile.setThreshold(metric, limits);
        hasThreshold = true;
    }
    if (!hasThreshold) return false;

    for (const JsonObjectConst rule : configuration["actuation_rules"].as<JsonArrayConst>()) {
        ActuationRule actuationRule{};
        if (!metricOf(rule["metric"].as<const char*>(), actuationRule.metric)
            || !stateOf(rule["state"].as<const char*>(), actuationRule.state)
            || !activationOf(rule["action"].as<const char*>(), actuationRule.action)) {
            return false;
        }
        if (!profile.addRule(actuationRule)) return false;
    }
    destination = profile;
    return true;
}

const char* ContainerProfileTranslator::actionNameOf(const ResponseAction action, const bool activation) {
    switch (action) {
        case ResponseAction::Cooling: return activation ? "COOLING_ON" : "COOLING_OFF";
        case ResponseAction::ServoOpen: return activation ? "SERVO_OPEN" : "SERVO_CLOSE";
        case ResponseAction::Ventilation:
        default: return activation ? "VENTILATION_ON" : "VENTILATION_OFF";
    }
}

bool ContainerProfileTranslator::metricOf(const char* name, ContainerMetric& metric) {
    if (name == nullptr) return false;
    if (strcmp(name, "TEMPERATURE") == 0) { metric = ContainerMetric::Temperature; return true; }
    if (strcmp(name, "HUMIDITY") == 0) { metric = ContainerMetric::Humidity; return true; }
    if (strcmp(name, "LUMINOSITY") == 0) { metric = ContainerMetric::Luminosity; return true; }
    return false;
}

bool ContainerProfileTranslator::stateOf(const char* name, EnvironmentalState& state) {
    if (name == nullptr) return false;
    if (strcmp(name, "WARNING") == 0) { state = EnvironmentalState::Warning; return true; }
    if (strcmp(name, "CRITICAL") == 0) { state = EnvironmentalState::Critical; return true; }
    return false; // A rule never reacts to NORMAL.
}

bool ContainerProfileTranslator::activationOf(const char* name, ResponseAction& action) {
    if (name == nullptr) return false;
    if (strcmp(name, "VENTILATION_ON") == 0) { action = ResponseAction::Ventilation; return true; }
    if (strcmp(name, "COOLING_ON") == 0) { action = ResponseAction::Cooling; return true; }
    if (strcmp(name, "SERVO_OPEN") == 0) { action = ResponseAction::ServoOpen; return true; }
    return false;
}

float ContainerProfileTranslator::limitOf(const JsonObjectConst threshold, const char* key) {
    const JsonVariantConst limit = threshold[key];
    return limit.is<float>() ? limit.as<float>() : NAN;
}
