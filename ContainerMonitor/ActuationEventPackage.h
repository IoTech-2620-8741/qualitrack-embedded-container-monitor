/**
 * @file ActuationEventPackage.h
 * @brief Action the Container Monitor executed (Edge route actuation-events, TS57).
 *
 * The Edge receives the action, its cause and its result:
 *
 * @code{.json}
 * { "action": "VENTILATION_ON", "trigger_metric": "TEMPERATURE", "trigger_state": "WARNING",
 *   "result": "EXECUTED", "occurred_at": "2026-11-13T15:00:05Z", "profile_version": 3 }
 * @endcode
 *
 * The cause is optional, but when present it names both the metric and its state. An opening or
 * closing of the container because of an RFID tag has no cause. `EdgeGatewayClient` adds the
 * `device_id` of the node.
 */
#ifndef ACTUATION_EVENT_PACKAGE_H
#define ACTUATION_EVENT_PACKAGE_H

#include <memory>
#include "ContainerProfile.h"
#include "EdgeTelemetryPackage.h"

/**
 * @brief One action of the container, as the Edge API receives it.
 */
class ActuationEventPackage : public EdgeTelemetryPackage {
public:
    /// Edge route of the actions of the container monitor.
    static constexpr const char* EDGE_ROUTE = "/api/v1/control/actuation-events";

    /**
     * @brief Creates an action caused by the state of a metric.
     * @param occurredAtEpochSeconds Unix time of the action.
     * @param action Response started or stopped.
     * @param activation True when the response starts (`*_ON`, `SERVO_OPEN`), false when it stops.
     * @param triggerMetric Metric whose state caused the action.
     * @param triggerState State of that metric.
     * @param executed True when the actuator did what was asked, false when it failed.
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The action, ready to be enqueued.
     */
    static std::unique_ptr<ActuationEventPackage> caused(uint32_t occurredAtEpochSeconds, ResponseAction action,
                                                         bool activation, ContainerMetric triggerMetric,
                                                         EnvironmentalState triggerState, bool executed,
                                                         uint32_t profileVersion);

    /**
     * @brief Creates an action without a metric cause, such as the opening for an authorized tag.
     * @param occurredAtEpochSeconds Unix time of the action.
     * @param action Response started or stopped.
     * @param activation True when the response starts, false when it stops.
     * @param executed True when the actuator did what was asked, false when it failed.
     * @param profileVersion Version of the profile the node applied (0 for the default profile).
     * @return The action, ready to be enqueued.
     */
    static std::unique_ptr<ActuationEventPackage> uncaused(uint32_t occurredAtEpochSeconds, ResponseAction action,
                                                           bool activation, bool executed, uint32_t profileVersion);

    ~ActuationEventPackage() override = default;

    [[nodiscard]] const char* getEdgeRoute() const override;

    /**
     * @brief Writes the fields of the Edge request (without the framework envelope).
     * @param serializationDestination JSON document of the request.
     */
    void serialize(JsonDocument& serializationDestination) const override;

private:
    ActuationEventPackage(uint32_t occurredAtEpochSeconds, ResponseAction action, bool activation, bool hasCause,
                          ContainerMetric triggerMetric, EnvironmentalState triggerState, bool executed,
                          uint32_t profileVersion);

    ResponseAction action;
    bool activation;
    bool hasCause;
    ContainerMetric triggerMetric;
    EnvironmentalState triggerState;
    bool executed;
};

#endif // ACTUATION_EVENT_PACKAGE_H
