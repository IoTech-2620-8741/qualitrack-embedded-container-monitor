/**
 * @file ContainerProfile.cpp
 * @brief Implements the classification of the container metrics with the rule of the cloud.
 */
#include "ContainerProfile.h"
#include <initializer_list>

namespace {
    /// True when the limit is present (the side of the range is not open).
    bool isPresent(const float limit) {
        return !std::isnan(limit);
    }

    /// Index of a metric in the arrays of the profile.
    size_t indexOf(const ContainerMetric metric) {
        return static_cast<size_t>(metric);
    }
}

MetricThreshold::MetricThreshold() : MetricThreshold(NAN, NAN, NAN, NAN) {}

MetricThreshold::MetricThreshold(const float normalMinimum, const float normalMaximum,
                                 const float criticalMinimum, const float criticalMaximum)
    : normalMinimum(normalMinimum),
      normalMaximum(normalMaximum),
      criticalMinimum(criticalMinimum),
      criticalMaximum(criticalMaximum) {}

bool MetricThreshold::isValid() const {
    const bool lowerSideComplete = isPresent(normalMinimum) == isPresent(criticalMinimum);
    const bool upperSideComplete = isPresent(normalMaximum) == isPresent(criticalMaximum);
    if (!lowerSideComplete || !upperSideComplete) return false;
    if (!isDefined()) return false;
    for (const float limit : {normalMinimum, normalMaximum, criticalMinimum, criticalMaximum}) {
        if (isPresent(limit) && std::isinf(limit)) return false;
    }
    if (isPresent(normalMinimum) && criticalMinimum >= normalMinimum) return false;
    if (isPresent(normalMaximum) && criticalMaximum <= normalMaximum) return false;
    if (isPresent(normalMinimum) && isPresent(normalMaximum) && normalMinimum >= normalMaximum) return false;
    return true;
}

bool MetricThreshold::isDefined() const {
    return isPresent(normalMinimum) || isPresent(normalMaximum);
}

EnvironmentalState MetricThreshold::classify(const float value) const {
    if (isPresent(criticalMinimum) && value < criticalMinimum) return EnvironmentalState::Critical;
    if (isPresent(criticalMaximum) && value > criticalMaximum) return EnvironmentalState::Critical;
    if (isPresent(normalMinimum) && value < normalMinimum) return EnvironmentalState::Warning;
    if (isPresent(normalMaximum) && value > normalMaximum) return EnvironmentalState::Warning;
    return EnvironmentalState::Normal;
}

EnvironmentalState MetricThreshold::evaluate(const float value, const EnvironmentalState previousState,
                                             const float hysteresisMargin) const {
    EnvironmentalState candidate = classify(value);
    // A worse (or the same) state applies at once; a better one only with the hysteresis margin.
    while (ContainerProfile::severityOf(candidate) < ContainerProfile::severityOf(previousState)
           && !isInsideRangeOf(candidate, value, hysteresisMargin)) {
        candidate = static_cast<EnvironmentalState>(ContainerProfile::severityOf(candidate) + 1);
    }
    return candidate;
}

bool MetricThreshold::isInsideRangeOf(const EnvironmentalState state, const float value, const float margin) const {
    switch (state) {
        case EnvironmentalState::Normal:
            return (!isPresent(normalMinimum) || value >= normalMinimum + margin)
                && (!isPresent(normalMaximum) || value <= normalMaximum - margin);
        case EnvironmentalState::Warning:
            return (!isPresent(criticalMinimum) || value >= criticalMinimum + margin)
                && (!isPresent(criticalMaximum) || value <= criticalMaximum - margin);
        case EnvironmentalState::Critical:
        default:
            return true;
    }
}

ContainerProfile ContainerProfile::defaultProfile() {
    ContainerProfile profile(DEFAULT_PROFILE_VERSION);
    profile.setThreshold(ContainerMetric::Temperature, MetricThreshold(15.0f, 25.0f, 10.0f, 30.0f));
    profile.setThreshold(ContainerMetric::Humidity, MetricThreshold(30.0f, 60.0f, 20.0f, 70.0f));
    profile.setThreshold(ContainerMetric::Luminosity, MetricThreshold(NAN, 200.0f, NAN, 500.0f));
    profile.addRule({ContainerMetric::Temperature, EnvironmentalState::Warning, ResponseAction::Ventilation});
    profile.addRule({ContainerMetric::Temperature, EnvironmentalState::Critical, ResponseAction::Cooling});
    return profile;
}

ContainerProfile::ContainerProfile(const uint32_t version)
    : rules{}, ruleCount(0), profileVersion(version) {}

void ContainerProfile::setThreshold(const ContainerMetric metric, const MetricThreshold& threshold) {
    thresholds[indexOf(metric)] = threshold;
}

bool ContainerProfile::addRule(const ActuationRule& rule) {
    if (ruleCount >= MAXIMUM_RULE_COUNT || !thresholdOf(rule.metric).isDefined()) return false;
    if (rule.state == EnvironmentalState::Normal) return false;
    rules[ruleCount++] = rule;
    return true;
}

const MetricThreshold& ContainerProfile::thresholdOf(const ContainerMetric metric) const {
    return thresholds[indexOf(metric)];
}

size_t ContainerProfile::getRuleCount() const {
    return ruleCount;
}

const ActuationRule& ContainerProfile::ruleAt(const size_t index) const {
    return rules[index];
}

uint32_t ContainerProfile::getVersion() const {
    return profileVersion;
}

float ContainerProfile::hysteresisOf(const ContainerMetric metric) {
    switch (metric) {
        case ContainerMetric::Temperature: return 0.5f;
        case ContainerMetric::Humidity: return 2.0f;
        case ContainerMetric::Luminosity:
        default: return 10.0f;
    }
}

const char* ContainerProfile::nameOf(const EnvironmentalState state) {
    switch (state) {
        case EnvironmentalState::Warning: return "WARNING";
        case EnvironmentalState::Critical: return "CRITICAL";
        case EnvironmentalState::Normal:
        default: return "NORMAL";
    }
}

const char* ContainerProfile::nameOf(const ContainerMetric metric) {
    switch (metric) {
        case ContainerMetric::Humidity: return "HUMIDITY";
        case ContainerMetric::Luminosity: return "LUMINOSITY";
        case ContainerMetric::Temperature:
        default: return "TEMPERATURE";
    }
}

uint8_t ContainerProfile::severityOf(const EnvironmentalState state) {
    return static_cast<uint8_t>(state);
}
