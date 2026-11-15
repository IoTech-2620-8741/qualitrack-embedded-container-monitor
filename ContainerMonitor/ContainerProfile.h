/**
 * @file ContainerProfile.h
 * @brief Environmental profile of a container: thresholds of each metric and the actuation rules.
 *
 * The Container Monitor applies the same rule as the QualiTrack cloud to each metric with thresholds
 * (temperature, humidity and luminosity): a value inside the normal range is NORMAL, outside it but
 * inside the critical range WARNING, and beyond the critical range CRITICAL. The limits belong to the
 * less severe state, and each side of a range is either complete (normal and critical limit) or open.
 *
 * To avoid switching an action on and off when a reading stays around a limit (MS10), the node only
 * returns to a less severe state once the value is inside its range by the hysteresis margin of the
 * metric.
 *
 * The actuation rules say which action the container starts when a metric reaches a state, for
 * example `TEMPERATURE` in `WARNING` starts `VENTILATION_ON`.
 */
#ifndef CONTAINER_PROFILE_H
#define CONTAINER_PROFILE_H

#include <cmath>
#include <cstddef>
#include <cstdint>

/** @brief Condition of a metric against the profile, as the cloud names it. */
enum class EnvironmentalState : uint8_t {
    Normal,   ///< Inside the normal range.
    Warning,  ///< Outside the normal range, inside the critical range.
    Critical  ///< Beyond the critical range.
};

/** @brief Metrics of the container with thresholds, as the cloud names them. */
enum class ContainerMetric : uint8_t {
    Temperature, ///< Temperature in °C.
    Humidity,    ///< Relative humidity in %.
    Luminosity   ///< Luminosity in lux.
};

/** @brief Number of metrics with thresholds. */
static constexpr size_t CONTAINER_METRIC_COUNT = 3;

/** @brief Actions the container starts because of a rule of the profile. */
enum class ResponseAction : uint8_t {
    Ventilation, ///< VENTILATION_ON / VENTILATION_OFF: the fan.
    Cooling,     ///< COOLING_ON / COOLING_OFF: simulated cooling, also on the fan.
    ServoOpen    ///< SERVO_OPEN / SERVO_CLOSE: the closing servo.
};

/** @brief Number of actions a rule can start. */
static constexpr size_t RESPONSE_ACTION_COUNT = 3;

/**
 * @brief Normal and critical range of one metric. A missing limit is stored as NaN (open side).
 */
class MetricThreshold {
public:
    /** @brief Creates a threshold with no limits (the metric is not evaluated). */
    MetricThreshold();

    /**
     * @brief Creates a threshold from the limits of the cloud.
     * @param normalMinimum Normal minimum, or NaN when the lower side is open.
     * @param normalMaximum Normal maximum, or NaN when the upper side is open.
     * @param criticalMinimum Critical minimum, or NaN when the lower side is open.
     * @param criticalMaximum Critical maximum, or NaN when the upper side is open.
     */
    MetricThreshold(float normalMinimum, float normalMaximum, float criticalMinimum, float criticalMaximum);

    /** @return True when the limits follow the rules of the cloud (complete, ordered sides). */
    [[nodiscard]] bool isValid() const;

    /** @return True when the threshold has at least one limit, so the metric is evaluated. */
    [[nodiscard]] bool isDefined() const;

    /**
     * @brief Classifies a value with the rule of the cloud, without hysteresis.
     * @param value Reading of the metric.
     * @return The state of the reading.
     */
    [[nodiscard]] EnvironmentalState classify(float value) const;

    /**
     * @brief Classifies a value keeping the previous state until the value leaves it by the margin.
     * @param value Reading of the metric.
     * @param previousState State the node showed before this reading.
     * @param hysteresisMargin Margin of the metric.
     * @return The state the node must apply now. A worse state applies at once.
     */
    [[nodiscard]] EnvironmentalState evaluate(float value, EnvironmentalState previousState,
                                              float hysteresisMargin) const;

private:
    float normalMinimum;
    float normalMaximum;
    float criticalMinimum;
    float criticalMaximum;

    [[nodiscard]] bool isInsideRangeOf(EnvironmentalState state, float value, float margin) const;
};

/** @brief Action a container starts when a metric reaches a state (or a worse one). */
struct ActuationRule {
    ContainerMetric metric;
    EnvironmentalState state;
    ResponseAction action;
};

/**
 * @brief Value object with the thresholds and rules of the profile the container applies.
 *
 * Version 0 is the default profile of the firmware, used until the Edge hands the node the profile of
 * the cloud.
 */
class ContainerProfile {
public:
    /// Maximum number of actuation rules the node keeps.
    static constexpr size_t MAXIMUM_RULE_COUNT = 12;
    /// Version of the default profile of the firmware (not a profile of the cloud).
    static constexpr uint32_t DEFAULT_PROFILE_VERSION = 0;

    /**
     * @brief Creates the default profile of a pharmaceutical container: 15–25 °C normal and 10–30 °C
     * critical, 30–60 % normal and 20–70 % critical humidity, up to 200 lux normal and 500 lux
     * critical, ventilation on temperature WARNING and cooling on temperature CRITICAL.
     */
    static ContainerProfile defaultProfile();

    /** @brief Creates an empty profile of a version (no thresholds, no rules). */
    explicit ContainerProfile(uint32_t version = DEFAULT_PROFILE_VERSION);

    /**
     * @brief Sets the threshold of a metric.
     * @param metric The metric.
     * @param threshold Its threshold.
     */
    void setThreshold(ContainerMetric metric, const MetricThreshold& threshold);

    /**
     * @brief Adds an actuation rule.
     * @param rule The rule; its metric needs a threshold.
     * @return False when the profile has no room left or the metric has no threshold.
     */
    bool addRule(const ActuationRule& rule);

    /** @return The threshold of a metric (an undefined threshold when the profile has none). */
    [[nodiscard]] const MetricThreshold& thresholdOf(ContainerMetric metric) const;

    /** @return Number of rules. */
    [[nodiscard]] size_t getRuleCount() const;

    /** @return The rule at an index lower than getRuleCount(). */
    [[nodiscard]] const ActuationRule& ruleAt(size_t index) const;

    /** @return Version of the profile; 0 for the default profile of the firmware. */
    [[nodiscard]] uint32_t getVersion() const;

    /**
     * @brief Margin a value must be inside a less severe range before the node returns to it.
     * @param metric The metric.
     * @return 0.5 °C, 2 % or 10 lux.
     */
    static float hysteresisOf(ContainerMetric metric);

    /** @return "NORMAL", "WARNING" or "CRITICAL". */
    static const char* nameOf(EnvironmentalState state);

    /** @return "TEMPERATURE", "HUMIDITY" or "LUMINOSITY". */
    static const char* nameOf(ContainerMetric metric);

    /** @return Severity order of a state, to compare two states. */
    static uint8_t severityOf(EnvironmentalState state);

private:
    MetricThreshold thresholds[CONTAINER_METRIC_COUNT];
    ActuationRule rules[MAXIMUM_RULE_COUNT];
    size_t ruleCount;
    uint32_t profileVersion;
};

#endif // CONTAINER_PROFILE_H
