/**
 * @file ActuationEventPackage.cpp
 * @brief Implements the actions of the Container Monitor in the format of the Edge API.
 */
#include "ActuationEventPackage.h"
#include "ContainerProfileTranslator.h"

ActuationEventPackage::ActuationEventPackage(const uint32_t occurredAtEpochSeconds, const ResponseAction action,
                                             const bool activation, const bool hasCause,
                                             const ContainerMetric triggerMetric,
                                             const EnvironmentalState triggerState, const bool executed,
                                             const uint32_t profileVersion)
    : EdgeTelemetryPackage(occurredAtEpochSeconds, profileVersion),
      action(action),
      activation(activation),
      hasCause(hasCause),
      triggerMetric(triggerMetric),
      triggerState(triggerState),
      executed(executed) {}

std::unique_ptr<ActuationEventPackage> ActuationEventPackage::caused(
        const uint32_t occurredAtEpochSeconds, const ResponseAction action, const bool activation,
        const ContainerMetric triggerMetric, const EnvironmentalState triggerState, const bool executed,
        const uint32_t profileVersion) {
    return std::unique_ptr<ActuationEventPackage>(new ActuationEventPackage(
        occurredAtEpochSeconds, action, activation, true, triggerMetric, triggerState, executed, profileVersion));
}

std::unique_ptr<ActuationEventPackage> ActuationEventPackage::uncaused(
        const uint32_t occurredAtEpochSeconds, const ResponseAction action, const bool activation,
        const bool executed, const uint32_t profileVersion) {
    return std::unique_ptr<ActuationEventPackage>(new ActuationEventPackage(
        occurredAtEpochSeconds, action, activation, false, ContainerMetric::Temperature,
        EnvironmentalState::Normal, executed, profileVersion));
}

const char* ActuationEventPackage::getEdgeRoute() const {
    return EDGE_ROUTE;
}

void ActuationEventPackage::serialize(JsonDocument& serializationDestination) const {
    serializationDestination["action"] = ContainerProfileTranslator::actionNameOf(action, activation);
    if (hasCause) {
        serializationDestination["trigger_metric"] = ContainerProfile::nameOf(triggerMetric);
        serializationDestination["trigger_state"] = ContainerProfile::nameOf(triggerState);
    }
    serializationDestination["result"] = executed ? "EXECUTED" : "FAILED";
    serializeObservationTime(serializationDestination, "occurred_at");
    serializeProfileVersion(serializationDestination);
}
