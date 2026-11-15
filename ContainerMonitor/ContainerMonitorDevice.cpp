/**
 * @file ContainerMonitorDevice.cpp
 * @brief Implements the reactions of the Container Monitor to its sensors, its tags and the Edge profile.
 */
#include "ContainerMonitorDevice.h"
#include <cctype>
#include <cmath>
#include <cstring>
#include <strings.h>
#include "ActuationEventPackage.h"
#include "ContainerProfileTranslator.h"
#include "ContainerReadingPackage.h"
#include "NodeLog.h"

namespace {
    constexpr const char* LOG_TAG = "ContainerMonitor";
    /// Stack of the sampler task: it also draws the screen and formats floats.
    constexpr uint32_t SAMPLER_STACK_BYTES = 6144;

    constexpr ContainerMetric ALL_METRICS[] = {
        ContainerMetric::Temperature, ContainerMetric::Humidity, ContainerMetric::Luminosity};
}

void ProfileMailbox::post(const ContainerProfile& profile) {
    portENTER_CRITICAL(&lock);
    pendingProfile = profile;
    hasPendingProfile = true;
    portEXIT_CRITICAL(&lock);
}

bool ProfileMailbox::take(ContainerProfile& destination) {
    portENTER_CRITICAL(&lock);
    const bool hadProfile = hasPendingProfile;
    if (hadProfile) {
        destination = pendingProfile;
        hasPendingProfile = false;
    }
    portEXIT_CRITICAL(&lock);
    return hadProfile;
}

ContainerMonitorDevice::ContainerMonitorDevice(const ContainerMonitorPins& pins,
                                               const ContainerMonitorSettings& settings,
                                               EdgeGatewayClient& gateway)
    : Device(SAMPLER_TICK_IN_MILLISECONDS),
      settings(settings),
      fanPin(pins.fan),
      climateSensor(pins.climateSensor, settings.climateSensorModel, this),
      lightSensor(LIGHT_SENSOR_IDENTIFIER, DigitalLightIntensitySensor::DEFAULT_I2C_ADDRESS,
                  DigitalLightIntensitySensor::DEFAULT_LIGHT_DETECTED_THRESHOLD_IN_LUX, this),
      tagReader(pins.tagReaderSelect, pins.tagReaderReset, this),
      lockServo(pins.lockServo, LOCK_CLOSED_ANGLE_IN_DEGREES, LOCK_OPEN_ANGLE_IN_DEGREES, this),
      fan(pins.fan, false, this),
      statusLed(pins.ledRed, pins.ledGreen, pins.ledBlue, false, this),
      alarmBuzzer(pins.buzzer, this),
      indicator(statusLed),
      display(DISPLAY_I2C_ADDRESS),
      gatewayClient(gateway),
      profile(ContainerProfile::defaultProfile()),
      values{NAN, NAN, NAN},
      states{EnvironmentalState::Normal, EnvironmentalState::Normal, EnvironmentalState::Normal},
      faulted{false, false, false},
      overallState(EnvironmentalState::Normal),
      responseActive{false, false, false},
      responseMetric{ContainerMetric::Temperature, ContainerMetric::Temperature, ContainerMetric::Temperature},
      lockOpen(false),
      openedByTag(false),
      openedByTagAt(0),
      alarmAcknowledged(false),
      serialTag{},
      serialTagLength(0)
{
    if (!display.initialize()) {
        NODE_LOG_E(LOG_TAG, "OLED not found at 0x%02X", DISPLAY_I2C_ADDRESS);
    }
    climateSensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
    lightSensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
    indicator.showStatus(LedPattern::Linking);
    indicator.refresh(millis());
    gatewayClient.setConfigurationListener(this);

    // The scheduler only accepts registrations before the engine starts.
    appendSensorToScheduler(&climateSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
    appendSensorToScheduler(&lightSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
    if (tagReader.isInitialized()) {
        appendSensorToScheduler(&tagReader, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
    } else {
        // Polling a missing reader would retry its 50 ms reset on every sample.
        NODE_LOG_W(LOG_TAG, "RC522 reader not found%s", settings.serialTagInput ? "; tags come from the serial monitor" : "");
    }
    appendSensorToScheduler(this, EVALUATION_EVENT_IDENTIFIER, EVALUATION_INTERVAL_IN_MILLISECONDS);
    appendSensorToScheduler(this, ANIMATION_EVENT_IDENTIFIER, ANIMATION_INTERVAL_IN_MILLISECONDS);
    appendSensorToScheduler(this, REPORT_EVENT_IDENTIFIER, REPORT_INTERVAL_IN_MILLISECONDS);
    appendSensorToScheduler(this, ALARM_REPEAT_EVENT_IDENTIFIER, ALARM_REPEAT_INTERVAL_IN_MILLISECONDS);

    EngineConfiguration engineConfiguration;
    engineConfiguration.samplerStackBytes = SAMPLER_STACK_BYTES;
    engineConfiguration.queueLength = QUEUE_LENGTH;
    initializeAsynchronousEngine(engineConfiguration);
    setTelemetryGateway(&gatewayClient);
    NODE_LOG_I(LOG_TAG, "Monitoring the container with the default profile; %u authorized tags",
               static_cast<unsigned>(settings.authorizedTagCount));
}

ContainerMonitorDevice::~ContainerMonitorDevice() {
    stopAsynchronousEngine();
}

void ContainerMonitorDevice::on(const Event event) {
    switch (event.identifier) {
        case EVALUATION_EVENT_IDENTIFIER:
            evaluate();
            break;
        case ANIMATION_EVENT_IDENTIFIER:
            animate();
            break;
        case REPORT_EVENT_IDENTIFIER:
            reportClimate();
            break;
        case ALARM_REPEAT_EVENT_IDENTIFIER:
            if (overallState == EnvironmentalState::Critical && !alarmAcknowledged) {
                playTone(CRITICAL_TONE_FREQUENCY_IN_HERTZ, CRITICAL_TONE_DURATION_IN_MILLISECONDS);
            }
            break;
        case RfidSensor::TAG_DETECTED_EVENT_IDENTIFIER:
            onTag(tagReader.getUid());
            break;
        case Sensor::SENSOR_FAULT_EVENT_IDENTIFIER:
            onSensorFault(event.sourceId, true);
            break;
        case Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER:
            onSensorFault(event.sourceId, false);
            break;
        default:
            // DATA_READ and the light events: the evaluation reads the latest sample of each sensor.
            break;
    }
}

void ContainerMonitorDevice::handle(Command command) {
    (void) command;
}

void ContainerMonitorDevice::onEdgeConfiguration(const JsonObjectConst configuration) {
    ContainerProfile edgeProfile;
    if (ContainerProfileTranslator::fromEdgeConfiguration(configuration, edgeProfile)) {
        profileMailbox.post(edgeProfile);
    } else {
        NODE_LOG_W(LOG_TAG, "The configuration of the Edge has no valid container profile");
    }
}

void ContainerMonitorDevice::processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) const {
    if (rawQueueItemPayload != nullptr) {
        gatewayClient.sendTelemetryRecord(*rawQueueItemPayload);
    }
}

void ContainerMonitorDevice::maintainNetworkServices() const {
    gatewayClient.service();
}

void ContainerMonitorDevice::evaluate() {
    const bool profileChanged = applyNewProfile();
    readMetrics();
    const EnvironmentalState previousOverall = overallState;
    // A new profile classifies at once; otherwise a better state needs the hysteresis margin (MS10).
    if (classifyMetrics(!profileChanged)) {
        soundStateChange(previousOverall);
        reportClimate(); // The Edge and the cloud learn about the change at once.
    }
    applyRules();
    updateStatus();
    refreshScreen(millis());
}

void ContainerMonitorDevice::animate() {
    if (settings.serialTagInput) {
        readSerialTag();
    }
    // Read after the tags: a tag read above stores a later millis(), and the unsigned difference
    // with an earlier one would end the opening at once.
    const unsigned long now = millis();
    if (openedByTag && now - openedByTagAt >= OPENING_DURATION_IN_MILLISECONDS) {
        openedByTag = false;
        NODE_LOG_I(LOG_TAG, "Opening time over");
        updateLock(false, ContainerMetric::Temperature);
    }
    indicator.refresh(now);
}

void ContainerMonitorDevice::readMetrics() {
    values[indexOf(ContainerMetric::Temperature)] = climateSensor.getTemperatureInCelsius();
    values[indexOf(ContainerMetric::Humidity)] = climateSensor.getRelativeHumidityInPercentage();
    values[indexOf(ContainerMetric::Luminosity)] = lightSensor.hasValidSample() ? lightSensor.getIlluminanceInLux() : NAN;
}

bool ContainerMonitorDevice::applyNewProfile() {
    ContainerProfile edgeProfile;
    if (!profileMailbox.take(edgeProfile) || edgeProfile.getVersion() == profile.getVersion()) return false;
    profile = edgeProfile;
    NODE_LOG_I(LOG_TAG, "Applying version %u of the container profile (%u rules)",
               static_cast<unsigned>(profile.getVersion()), static_cast<unsigned>(profile.getRuleCount()));
    return true;
}

bool ContainerMonitorDevice::classifyMetrics(const bool withHysteresis) {
    bool changed = false;
    EnvironmentalState worst = EnvironmentalState::Normal;
    for (const ContainerMetric metric : ALL_METRICS) {
        const size_t index = indexOf(metric);
        const MetricThreshold& threshold = profile.thresholdOf(metric);
        EnvironmentalState state = EnvironmentalState::Normal;
        if (threshold.isDefined() && !faulted[index] && !std::isnan(values[index])) {
            state = withHysteresis
                ? threshold.evaluate(values[index], states[index], ContainerProfile::hysteresisOf(metric))
                : threshold.classify(values[index]);
        }
        if (state != states[index]) {
            NODE_LOG_I(LOG_TAG, "%s %.1f: %s -> %s", ContainerProfile::nameOf(metric), values[index],
                       ContainerProfile::nameOf(states[index]), ContainerProfile::nameOf(state));
            states[index] = state;
            changed = true;
        }
        if (ContainerProfile::severityOf(state) > ContainerProfile::severityOf(worst)) worst = state;
    }
    overallState = worst;
    return changed;
}

void ContainerMonitorDevice::applyRules() {
    bool desired[RESPONSE_ACTION_COUNT] = {false, false, false};
    ContainerMetric cause[RESPONSE_ACTION_COUNT] = {
        ContainerMetric::Temperature, ContainerMetric::Temperature, ContainerMetric::Temperature};
    for (size_t ruleIndex = 0; ruleIndex < profile.getRuleCount(); ++ruleIndex) {
        const ActuationRule& rule = profile.ruleAt(ruleIndex);
        const size_t action = indexOf(rule.action);
        // A rule of WARNING keeps acting when the metric gets worse.
        if (!desired[action] && ContainerProfile::severityOf(states[indexOf(rule.metric)])
                                >= ContainerProfile::severityOf(rule.state)) {
            desired[action] = true;
            cause[action] = rule.metric;
        }
    }
    setResponse(ResponseAction::Ventilation, desired[indexOf(ResponseAction::Ventilation)],
                cause[indexOf(ResponseAction::Ventilation)]);
    setResponse(ResponseAction::Cooling, desired[indexOf(ResponseAction::Cooling)],
                cause[indexOf(ResponseAction::Cooling)]);
    setResponse(ResponseAction::ServoOpen, desired[indexOf(ResponseAction::ServoOpen)],
                cause[indexOf(ResponseAction::ServoOpen)]);
}

void ContainerMonitorDevice::setResponse(const ResponseAction action, const bool active, ContainerMetric metric) {
    const size_t index = indexOf(action);
    if (responseActive[index] == active) return;
    if (active) {
        responseMetric[index] = metric;
    } else {
        metric = responseMetric[index]; // The response stops because its metric got better.
    }
    responseActive[index] = active;

    if (action == ResponseAction::ServoOpen) {
        updateLock(true, metric);
        return;
    }
    driveFan();
    const bool fanExpected = responseActive[indexOf(ResponseAction::Ventilation)]
                             || responseActive[indexOf(ResponseAction::Cooling)];
    const bool executed = (digitalRead(fanPin) == HIGH) == fanExpected;
    NODE_LOG_I(LOG_TAG, "%s (%s %s)%s", ContainerProfileTranslator::actionNameOf(action, active),
               ContainerProfile::nameOf(metric), ContainerProfile::nameOf(states[indexOf(metric)]),
               executed ? "" : " FAILED");
    reportAction(action, active, true, metric, executed);
}

void ContainerMonitorDevice::driveFan() {
    // Ventilation and simulated cooling share the fan of the container.
    const bool on = responseActive[indexOf(ResponseAction::Ventilation)]
                    || responseActive[indexOf(ResponseAction::Cooling)];
    fan.handle(on ? RelayActuator::RELAY_ON_COMMAND : RelayActuator::RELAY_OFF_COMMAND);
}

void ContainerMonitorDevice::updateLock(const bool causedByRule, const ContainerMetric metric) {
    const bool open = openedByTag || responseActive[indexOf(ResponseAction::ServoOpen)];
    if (open == lockOpen) return;
    lockServo.handle(open ? LockServo::OPEN_COMMAND : LockServo::CLOSE_COMMAND);
    const bool executed = lockServo.isOpen() == open;
    if (executed) lockOpen = open;
    NODE_LOG_I(LOG_TAG, "%s%s", ContainerProfileTranslator::actionNameOf(ResponseAction::ServoOpen, open),
               executed ? "" : " FAILED");
    reportAction(ResponseAction::ServoOpen, open, causedByRule, metric, executed);
}

void ContainerMonitorDevice::onTag(const char* tag) {
    if (tag == nullptr || tag[0] == '\0') return;
    const unsigned long now = millis();
    const bool granted = isAuthorized(tag);
    NODE_LOG_I(LOG_TAG, "Tag %s: access %s", tag, granted ? "granted" : "denied");

    if (EdgeGatewayClient::isClockSynchronized()) {
        enqueueTelemetryPayload(ContainerReadingPackage::rfidTag(
            TimeSynchronizationDriver::getCurrentTimestamp(), tag, profile.getVersion()));
    } else {
        NODE_LOG_W(LOG_TAG, "Tag %s not sent: clock not synchronized", tag);
    }

    display.showAccess(granted, tag, now);
    if (granted) {
        indicator.showFeedback(LedPattern::AccessGranted, now);
        if (overallState == EnvironmentalState::Critical && !alarmAcknowledged) {
            alarmAcknowledged = true; // US74: the staff acknowledges the local alarm.
            NODE_LOG_I(LOG_TAG, "Alarm acknowledged by %s", tag);
        }
        playTone(GRANTED_TONE_FREQUENCY_IN_HERTZ, GRANTED_TONE_DURATION_IN_MILLISECONDS);
        openedByTag = true;
        openedByTagAt = now; // A new tag while open restarts the opening time.
        updateLock(false, ContainerMetric::Temperature);
    } else {
        indicator.showFeedback(LedPattern::AccessDenied, now);
        playTone(DENIED_TONE_FREQUENCY_IN_HERTZ, DENIED_TONE_DURATION_IN_MILLISECONDS);
    }
    refreshScreen(now);
}

bool ContainerMonitorDevice::isAuthorized(const char* tag) const {
    for (size_t index = 0; index < settings.authorizedTagCount; ++index) {
        const char* authorized = settings.authorizedTags[index];
        if (authorized != nullptr && strcasecmp(authorized, tag) == 0) return true;
    }
    return false;
}

void ContainerMonitorDevice::readSerialTag() {
    while (Serial.available() > 0) {
        const int character = Serial.read();
        if (character == '\n' || character == '\r') {
            if (serialTagLength > 0) {
                serialTag[serialTagLength] = '\0';
                serialTagLength = 0;
                onTag(serialTag);
            }
        } else if (character > ' ' && serialTagLength < sizeof(serialTag) - 1) {
            serialTag[serialTagLength++] = static_cast<char>(toupper(character));
        }
    }
}

void ContainerMonitorDevice::onSensorFault(const int sourceId, const bool fault) {
    if (sourceId == climateSensor.getPin()) {
        faulted[indexOf(ContainerMetric::Temperature)] = fault;
        faulted[indexOf(ContainerMetric::Humidity)] = fault;
        NODE_LOG_E(LOG_TAG, "DHT sensor %s", fault ? "fault" : "recovered");
    } else if (sourceId == LIGHT_SENSOR_IDENTIFIER) {
        faulted[indexOf(ContainerMetric::Luminosity)] = fault;
        if (fault) lightSensor.initialize(); // Retry the setup of the BH1750 in case it was reconnected.
        NODE_LOG_E(LOG_TAG, "BH1750 sensor %s", fault ? "fault" : "recovered");
    }
    updateStatus();
}

void ContainerMonitorDevice::soundStateChange(const EnvironmentalState previousOverall) {
    if (overallState != EnvironmentalState::Critical) {
        alarmAcknowledged = false; // A new CRITICAL needs a new acknowledgement.
    }
    if (ContainerProfile::severityOf(overallState) <= ContainerProfile::severityOf(previousOverall)) {
        if (overallState == EnvironmentalState::Normal) alarmBuzzer.handle(Buzzer::TURN_OFF_COMMAND);
        return;
    }
    if (overallState == EnvironmentalState::Critical) {
        playTone(CRITICAL_TONE_FREQUENCY_IN_HERTZ, CRITICAL_TONE_DURATION_IN_MILLISECONDS);
    } else {
        playTone(WARNING_TONE_FREQUENCY_IN_HERTZ, WARNING_TONE_DURATION_IN_MILLISECONDS);
    }
}

void ContainerMonitorDevice::updateStatus() {
    LedPattern pattern = LedPattern::Normal;
    if (overallState == EnvironmentalState::Critical) pattern = LedPattern::Critical;
    else if (needsReview()) pattern = LedPattern::NeedsReview;
    else if (overallState == EnvironmentalState::Warning) pattern = LedPattern::Warning;
    else if (edgeLink() == EdgeLink::Linking) pattern = LedPattern::Linking;
    indicator.showStatus(pattern);
}

void ContainerMonitorDevice::refreshScreen(const unsigned long nowInMilliseconds) {
    ContainerSnapshot snapshot{};
    snapshot.deviceId = settings.deviceId;
    for (const ContainerMetric metric : ALL_METRICS) {
        const size_t index = indexOf(metric);
        snapshot.values[index] = values[index];
        snapshot.states[index] = states[index];
        snapshot.faulted[index] = faulted[index];
    }
    snapshot.overallState = overallState;
    snapshot.locked = !lockOpen;
    snapshot.ventilationOn = responseActive[indexOf(ResponseAction::Ventilation)];
    snapshot.coolingOn = responseActive[indexOf(ResponseAction::Cooling)];
    snapshot.alarmAcknowledged = alarmAcknowledged;
    snapshot.link = edgeLink();
    display.refresh(snapshot, nowInMilliseconds);
}

void ContainerMonitorDevice::reportClimate() {
    float reported[CONTAINER_METRIC_COUNT];
    bool hasValue = false;
    for (const ContainerMetric metric : ALL_METRICS) {
        const size_t index = indexOf(metric);
        reported[index] = faulted[index] ? NAN : values[index];
        hasValue = hasValue || !std::isnan(reported[index]);
    }
    if (!hasValue) return;
    if (!EdgeGatewayClient::isClockSynchronized()) {
        NODE_LOG_W(LOG_TAG, "Climate not sent: clock not synchronized");
        return;
    }
    enqueueTelemetryPayload(ContainerReadingPackage::climate(
        TimeSynchronizationDriver::getCurrentTimestamp(), reported[indexOf(ContainerMetric::Temperature)],
        reported[indexOf(ContainerMetric::Humidity)], reported[indexOf(ContainerMetric::Luminosity)],
        profile.getVersion()));
}

void ContainerMonitorDevice::reportAction(const ResponseAction action, const bool activation, const bool hasCause,
                                          const ContainerMetric metric, const bool executed) {
    if (!EdgeGatewayClient::isClockSynchronized()) {
        NODE_LOG_W(LOG_TAG, "%s not sent: clock not synchronized",
                   ContainerProfileTranslator::actionNameOf(action, activation));
        return;
    }
    const uint32_t now = TimeSynchronizationDriver::getCurrentTimestamp();
    if (hasCause) {
        enqueueTelemetryPayload(ActuationEventPackage::caused(now, action, activation, metric,
                                                              states[indexOf(metric)], executed,
                                                              profile.getVersion()));
    } else {
        enqueueTelemetryPayload(ActuationEventPackage::uncaused(now, action, activation, executed,
                                                                profile.getVersion()));
    }
}

bool ContainerMonitorDevice::needsReview() const {
    for (const bool sensorFaulted : faulted) {
        if (sensorFaulted) return true;
    }
    return gatewayClient.getMillisecondsSinceLastContact() >= REVIEW_TIMEOUT_IN_MILLISECONDS;
}

EdgeLink ContainerMonitorDevice::edgeLink() const {
    if (gatewayClient.getMillisecondsSinceLastContact() >= REVIEW_TIMEOUT_IN_MILLISECONDS) return EdgeLink::Offline;
    return gatewayClient.isSignedIn() ? EdgeLink::Online : EdgeLink::Linking;
}

void ContainerMonitorDevice::playTone(const int frequencyInHertz, const unsigned long durationInMilliseconds) {
    alarmBuzzer.setTone(frequencyInHertz, durationInMilliseconds);
    alarmBuzzer.handle(Buzzer::PLAY_TONE_COMMAND);
}

size_t ContainerMonitorDevice::indexOf(const ContainerMetric metric) {
    return static_cast<size_t>(metric);
}

size_t ContainerMonitorDevice::indexOf(const ResponseAction action) {
    return static_cast<size_t>(action);
}
