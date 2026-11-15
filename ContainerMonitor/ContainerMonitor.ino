/**
 * @file ContainerMonitor.ino
 * @brief Embedded Application of the QualiTrack Container Monitor (ESP32 DevKit v1) on the Modest IoT Nano Framework.
 *
 * One Container Monitor watches each container of drug lots: it measures the temperature, humidity (DHT) and
 * luminosity (BH1750) against the environmental profile of the container, starts the ventilation or simulated cooling
 * with the rules of the profile, opens the lock (SG90 servo) only for the RFID tags (RC522) of the authorized staff,
 * shows its state on an OLED, an RGB LED and a buzzer, and sends its readings and actions to the QualiTrack Edge
 * Service of the local station, which synchronizes them with the cloud.
 *
 * | Story | Behavior |
 * |-------|----------|
 * | MS06 | Temperature and humidity of the container (DHT) |
 * | MS07 | Luminosity of the container (BH1750) |
 * | MS08 | OLED with the data, the lock, the action and the link with the Edge |
 * | MS09 | RFID tag of the person who opens the container (RC522) |
 * | MS10 | Ventilation and simulated cooling with the rules of the profile, with hysteresis |
 * | MS11 | Lock opened for 10 seconds for an authorized tag |
 * | MS12 | Climate sent when a state changes and every minute; every tag read is sent |
 * | MS13 | Every action sent with its cause and its result |
 * | MS14 | Sign-in with the identifier and the API key of the node |
 * | MS15 | Container profile read from the Edge every five minutes and applied |
 * | US71 | Status LED: green normal, yellow warning, red critical, solid red needs review, blue linking |
 * | US74 | The local alarm is acknowledged with an authorized tag |
 *
 * The sketch takes from the framework the `DigitalLightIntensitySensor`, `RfidSensor`, `ServoMotor`, `RelayActuator`,
 * `RgbLed`, `Buzzer`, `WiFiConnectivityDriver`, `TimeSynchronizationDriver` and `HttpGatewayClient`, and writes what
 * the framework asks an application to write: the telemetry packages of the Edge and the device that reacts to the
 * events. The `DhtSensor` and the `OledDisplay` of the framework v1.0.0 do not work (see the README), so the DHT22 is
 * read by `ClimateSensor` and the OLED is drawn by `ContainerDisplay`, two files of this sketch.
 *
 * Requirements: ESP32 core 3.x and the library Modest-IoT-Nano-Framework 1.0.0 (Arduino Library Manager, with
 * ArduinoJson and MQTT). Copy secrets.example.h as secrets.h with the values of the node.
 *
 * Circuit of report section 5.6:
 *
 * | Component | ESP32 |
 * |-----------|-------|
 * | OLED SSD1306 and BH1750 (I2C) SDA / SCL | GPIO 21 / 22 |
 * | RC522 (SPI) MOSI / MISO / SCK / SDA / RST | GPIO 23 / 19 / 18 / 5 / 2 |
 * | DHT22 data | GPIO 15 |
 * | SG90 servo signal | GPIO 13 |
 * | 2N2222 base of the fan (1 kΩ) | GPIO 12 |
 * | RGB LED red / green / blue (220 Ω each, common cathode) | GPIO 4 / 16 / 17 |
 * | Buzzer | GPIO 25 |
 */
#include <ModestIoT.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <ctime>
#include <memory>
#include <strings.h>
#include "ClimateSensor.h"
#include "ContainerDisplay.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h" // Example values, enough for Wokwi.
#endif

/** @name Pins of the circuit */
//@{
static constexpr int I2C_SDA_PIN = 21;
static constexpr int I2C_SCL_PIN = 22;
static constexpr int CLIMATE_SENSOR_PIN = 15;
static constexpr int TAG_READER_SELECT_PIN = 5;
static constexpr int TAG_READER_RESET_PIN = 2;
static constexpr int LOCK_SERVO_PIN = 13;
static constexpr int FAN_PIN = 12;
static constexpr int LED_RED_PIN = 4;
static constexpr int LED_GREEN_PIN = 16;
static constexpr int LED_BLUE_PIN = 17;
static constexpr int BUZZER_PIN = 25;
//@}

/** @name Routes of the QualiTrack Edge API */
//@{
static constexpr const char* SIGN_IN_ROUTE = "/api/v1/auth/sign-in";
static constexpr const char* READINGS_ROUTE = "/api/v1/monitoring/container-readings";
static constexpr const char* ACTUATION_EVENTS_ROUTE = "/api/v1/control/actuation-events";
static constexpr const char* CONFIGURATION_ROUTE = "/api/v1/configurations/";
//@}

/// Speed of the I2C bus shared by the OLED and the BH1750 (a full frame takes about 25 ms).
static constexpr uint32_t I2C_CLOCK_IN_HERTZ = 400000;
static constexpr unsigned long SERIAL_BAUD_RATE = 115200;
static constexpr unsigned long MAIN_LOOP_DELAY_IN_MILLISECONDS = 60000;

/** @name Vocabulary of the container profile, in the order of the Edge API names */
//@{
enum class State : uint8_t { Normal, Warning, Critical };
enum class Metric : uint8_t { Temperature, Humidity, Luminosity };
enum class Action : uint8_t { Ventilation, Cooling, ServoOpen };

static constexpr size_t METRIC_COUNT = 3;
static constexpr size_t ACTION_COUNT = 3;
static const char* const STATE_NAMES[] = {"NORMAL", "WARNING", "CRITICAL"};
static const char* const METRIC_NAMES[] = {"TEMPERATURE", "HUMIDITY", "LUMINOSITY"};
static const char* const ACTION_ON_NAMES[] = {"VENTILATION_ON", "COOLING_ON", "SERVO_OPEN"};
static const char* const ACTION_OFF_NAMES[] = {"VENTILATION_OFF", "COOLING_OFF", "SERVO_CLOSE"};
/// Margin to leave a state, per metric, so the actions do not flicker: 0.5 °C, 2 % and 10 lux.
static constexpr float HYSTERESIS[] = {0.5f, 2.0f, 10.0f};

//@}

/** @brief Names of the Edge API (in a struct, so the Arduino builder adds no prototypes for them). */
struct Names {
    static const char* of(State state) { return STATE_NAMES[static_cast<int>(state)]; }
    static const char* of(Metric metric) { return METRIC_NAMES[static_cast<int>(metric)]; }
    static const char* of(Action action, bool on) {
        return (on ? ACTION_ON_NAMES : ACTION_OFF_NAMES)[static_cast<int>(action)];
    }

    /** @return The index of the name in the list, or -1. */
    static int indexIn(const char* const* names, size_t count, const char* name) {
        for (size_t i = 0; name != nullptr && i < count; i++) {
            if (strcmp(names[i], name) == 0) return static_cast<int>(i);
        }
        return -1;
    }
};

/// Authorization header of every request: the Edge takes the API key of the node as a Bearer token (Edge ADR-0018).
static char authorizationHeader[128];

/**
 * @brief Limits of one metric (NAN when a side has no limit), classified with the rule of the cloud.
 */
struct Threshold {
    float normalMin = NAN, normalMax = NAN, criticalMin = NAN, criticalMax = NAN;

    bool isDefined() const { return !std::isnan(normalMin) || !std::isnan(normalMax); }

    State classify(float value) const {
        if (value < criticalMin || value > criticalMax) return State::Critical; // false for NAN limits
        if (value < normalMin || value > normalMax) return State::Warning;
        return State::Normal;
    }

    /** @brief A worse state applies at once; a better one only when the value is `margin` inside its range. */
    State evaluate(float value, State previous, float margin) const {
        State next = classify(value);
        while (next < previous && !isInside(next, value, margin)) next = static_cast<State>(static_cast<int>(next) + 1);
        return next;
    }

    /** @brief Same rule as the cloud: complete sides, at least one, and critical limits outside the normal ones. */
    bool isValid() const {
        if (std::isnan(normalMin) != std::isnan(criticalMin) || std::isnan(normalMax) != std::isnan(criticalMax)) return false;
        if (!isDefined()) return false;
        if (!std::isnan(normalMin) && criticalMin >= normalMin) return false;
        if (!std::isnan(normalMax) && criticalMax <= normalMax) return false;
        return std::isnan(normalMin) || std::isnan(normalMax) || normalMin < normalMax;
    }

private:
    bool isInside(State state, float value, float margin) const {
        if (state == State::Normal) {
            return !(value < normalMin + margin) && !(value > normalMax - margin);
        }
        if (state == State::Warning) {
            return !(value < criticalMin + margin) && !(value > criticalMax - margin);
        }
        return true;
    }
};

/** @brief Action a container starts while a metric is in a state or worse (a WARNING rule keeps acting on CRITICAL). */
struct Rule {
    Metric metric;
    State state;
    Action action;
};

/** @brief Thresholds, actuation rules and version of the container profile. */
struct Profile {
    static constexpr size_t MAXIMUM_RULE_COUNT = 12;

    Threshold thresholds[METRIC_COUNT];
    Rule rules[MAXIMUM_RULE_COUNT]{};
    size_t ruleCount = 0;
    uint32_t version = 0; // 0 = default profile of the node, until the Edge hands one.

    /** @brief Profile of the node while the Edge has none: 15-25 °C, 30-60 % and up to 200 lux. */
    static Profile defaults() {
        Profile profile;
        profile.thresholds[0] = {15, 25, 10, 30};
        profile.thresholds[1] = {30, 60, 20, 70};
        profile.thresholds[2] = {NAN, 200, NAN, 500};
        profile.addRule({Metric::Temperature, State::Warning, Action::Ventilation});
        profile.addRule({Metric::Temperature, State::Critical, Action::Cooling});
        return profile;
    }

    bool addRule(const Rule& rule) {
        if (ruleCount >= MAXIMUM_RULE_COUNT || rule.state == State::Normal) return false;
        if (!thresholds[static_cast<int>(rule.metric)].isDefined()) return false;
        rules[ruleCount++] = rule;
        return true;
    }
};

/**
 * @brief One record for the Edge, in the JSON the Edge expects.
 *
 * Records are sent with `HttpGatewayClient::sendRequest()`, because the Edge accepts only its own fields and
 * `sendTelemetryRecord()` would add the `timestamp` and `sequence` of the framework.
 */
class EdgeRecord : public TelemetryPackage {
public:
    /** @return True for an actuation event, false for a container reading. */
    virtual bool isActuationEvent() const = 0;

protected:
    EdgeRecord(uint32_t observedAt, uint32_t profileVersion) : observedAt(observedAt), profileVersion(profileVersion) {}

    void serializeCommon(JsonDocument& json, const char* timeKey) const {
        char isoTime[21];
        const time_t seconds = observedAt;
        tm utc{};
        gmtime_r(&seconds, &utc);
        strftime(isoTime, sizeof(isoTime), "%Y-%m-%dT%H:%M:%SZ", &utc);
        json["device_id"] = DEVICE_ID;
        json[timeKey] = isoTime; // A char array is copied into the document.
        if (profileVersion != 0) json["profile_version"] = profileVersion;
    }

private:
    uint32_t observedAt;
    uint32_t profileVersion;
};

/** @brief The climate of the container, or one RFID tag read, at one moment (`POST container-readings`). */
class ContainerReading : public EdgeRecord {
public:
    static std::unique_ptr<ContainerReading> climate(uint32_t observedAt, float temperature, float humidity,
                                                     float luminosity, uint32_t profileVersion) {
        std::unique_ptr<ContainerReading> reading(new ContainerReading(observedAt, profileVersion));
        reading->values[0] = temperature;
        reading->values[1] = humidity;
        reading->values[2] = luminosity;
        return reading;
    }

    static std::unique_ptr<ContainerReading> rfidTag(uint32_t observedAt, const char* tag, uint32_t profileVersion) {
        std::unique_ptr<ContainerReading> reading(new ContainerReading(observedAt, profileVersion));
        strncpy(reading->tag, tag, sizeof(reading->tag) - 1);
        return reading;
    }

    bool isActuationEvent() const override { return false; }

    void serialize(JsonDocument& json) const override {
        serializeCommon(json, "measured_at");
        static const char* const KEYS[] = {"temperature", "humidity", "luminosity"};
        for (size_t i = 0; i < METRIC_COUNT; i++) {
            if (!std::isnan(values[i])) json[KEYS[i]] = roundf(values[i] * 10.0f) / 10.0f;
        }
        if (tag[0] != '\0') json["rfid_tag"] = static_cast<const char*>(tag);
    }

private:
    ContainerReading(uint32_t observedAt, uint32_t profileVersion) : EdgeRecord(observedAt, profileVersion) {}

    float values[METRIC_COUNT] = {NAN, NAN, NAN};
    char tag[32]{};
};

/** @brief One action the node executed, with the metric and state that caused it (`POST actuation-events`). */
class ActuationEvent : public EdgeRecord {
public:
    /** @param hasCause False for the lock opened by a tag, which no metric caused. */
    ActuationEvent(uint32_t occurredAt, Action action, bool on, bool hasCause, Metric metric, State state,
                   bool executed, uint32_t profileVersion)
        : EdgeRecord(occurredAt, profileVersion), action(action), on(on), hasCause(hasCause), metric(metric),
          state(state), executed(executed) {}

    bool isActuationEvent() const override { return true; }

    void serialize(JsonDocument& json) const override {
        json["action"] = Names::of(action, on);
        if (hasCause) { // The Edge takes both trigger fields or neither.
            json["trigger_metric"] = Names::of(metric);
            json["trigger_state"] = Names::of(state);
        }
        json["result"] = executed ? "EXECUTED" : "FAILED";
        serializeCommon(json, "occurred_at");
    }

private:
    Action action;
    bool on, hasCause;
    Metric metric;
    State state;
    bool executed;
};

/**
 * @brief Container Monitor: reacts on the sampler task to its sensors and tags, and talks to the Edge on the network
 * task. The local reaction does not depend on the network: with no Edge, it keeps the default profile.
 */
class ContainerMonitor : public Device {
public:
    ContainerMonitor(WiFiConnectivityDriver& wifiDriver, HttpGatewayClient& edgeGateway)
        : Device(SAMPLER_TICK_IN_MILLISECONDS),
          climateSensor(CLIMATE_SENSOR_PIN, ClimateSensor::MODEL_DHT22, this),
          lightSensor(LIGHT_SENSOR_IDENTIFIER, DigitalLightIntensitySensor::DEFAULT_I2C_ADDRESS,
                      DigitalLightIntensitySensor::DEFAULT_LIGHT_DETECTED_THRESHOLD_IN_LUX, this),
          tagReader(TAG_READER_SELECT_PIN, TAG_READER_RESET_PIN, this),
          lockServo(LOCK_SERVO_PIN, LOCK_CLOSED_ANGLE, this),
          fan(FAN_PIN, false, this),
          statusLed(LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN, false, this),
          alarmBuzzer(BUZZER_PIN, this),
          wifi(wifiDriver),
          edge(edgeGateway),
          profile(Profile::defaults())
    {
        snprintf(signInUrl, sizeof(signInUrl), "%s%s", EDGE_BASE_URL, SIGN_IN_ROUTE);
        snprintf(readingsUrl, sizeof(readingsUrl), "%s%s", EDGE_BASE_URL, READINGS_ROUTE);
        snprintf(actuationEventsUrl, sizeof(actuationEventsUrl), "%s%s", EDGE_BASE_URL, ACTUATION_EVENTS_ROUTE);
        snprintf(configurationUrl, sizeof(configurationUrl), "%s%s%s", EDGE_BASE_URL, CONFIGURATION_ROUTE, DEVICE_ID);

        if (!display.initialize()) Serial.println("[ContainerMonitor] OLED not found at 0x3C");
        climateSensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
        lightSensor.setFaultThreshold(SENSOR_FAULT_THRESHOLD);
        animateLed(millis());

        // The scheduler only accepts registrations before the engine starts.
        appendSensorToScheduler(&climateSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        appendSensorToScheduler(&lightSensor, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        if (tagReader.isInitialized()) {
            appendSensorToScheduler(&tagReader, Sensor::MEASURE_DATA_REQUESTED_EVENT_IDENTIFIER);
        } else { // Polling a missing reader would retry its 50 ms reset on every sample.
            Serial.printf("[ContainerMonitor] RC522 reader not found%s\n",
                          SERIAL_TAG_INPUT ? "; tags come from the serial monitor" : "");
        }
        appendSensorToScheduler(this, EVALUATE_EVENT, 1000);    // Classify, act and draw every second.
        appendSensorToScheduler(this, ANIMATE_EVENT, 100);      // LED patterns, serial tags and end of the opening.
        appendSensorToScheduler(this, REPORT_EVENT, 60000);     // Send the climate every minute.
        appendSensorToScheduler(this, ALARM_REPEAT_EVENT, 2000); // Beep every two seconds while CRITICAL.

        EngineConfiguration engine;
        engine.samplerStackBytes = SAMPLER_STACK_BYTES; // The sampler also draws the screen and formats floats.
        engine.queueLength = QUEUE_LENGTH;              // Also the command queue that carries a whole profile.
        initializeAsynchronousEngine(engine);
        Serial.printf("[ContainerMonitor] Monitoring with the default profile; %u authorized tags\n",
                      static_cast<unsigned>(sizeof(AUTHORIZED_TAGS) / sizeof(AUTHORIZED_TAGS[0])));
    }

    ~ContainerMonitor() override { stopAsynchronousEngine(); }

    /** @runs_on{sampler} */
    void on(Event event) override {
        switch (event.identifier) {
            case EVALUATE_EVENT: evaluate(false); break;
            case ANIMATE_EVENT: animate(); break;
            case REPORT_EVENT: sendClimate(); break;
            case ALARM_REPEAT_EVENT:
                if (overall == State::Critical && !alarmAcknowledged) playTone(2000, 500);
                break;
            case RfidSensor::TAG_DETECTED_EVENT_IDENTIFIER: onTag(tagReader.getUid()); break;
            case Sensor::SENSOR_FAULT_EVENT_IDENTIFIER: onSensorFault(event.sourceId, true); break;
            case Sensor::SENSOR_RECOVERED_EVENT_IDENTIFIER: onSensorFault(event.sourceId, false); break;
            default: break; // DATA_READ: the evaluation reads the latest sample of each sensor.
        }
    }

    /**
     * @brief Receives the container profile of the Edge: a start, one limit or rule per command, then its version.
     * @runs_on{sampler}
     */
    void handle(Command command) override {
        const int id = command.identifier;
        if (id == BEGIN_PROFILE) {
            pending = Profile();
            pendingVersion = static_cast<uint32_t>(command.payload);
            receiving = true;
        } else if (!receiving) {
            return; // The start of this profile was lost: the network task reads it again.
        } else if (id >= SET_LIMIT && id < SET_LIMIT + static_cast<int>(METRIC_COUNT * 4)) {
            Threshold& threshold = pending.thresholds[(id - SET_LIMIT) / 4];
            float* limits[] = {&threshold.normalMin, &threshold.normalMax, &threshold.criticalMin, &threshold.criticalMax};
            *limits[(id - SET_LIMIT) % 4] = command.payload == NO_LIMIT ? NAN : command.payload / 100.0f;
        } else if (id == ADD_RULE) {
            pending.addRule({static_cast<Metric>(command.payload & 0xFF), static_cast<State>((command.payload >> 8) & 0xFF),
                             static_cast<Action>((command.payload >> 16) & 0xFF)});
        } else if (id == APPLY_PROFILE) {
            receiving = false;
            if (static_cast<uint32_t>(command.payload) != pendingVersion || pendingVersion == profile.version) return;
            pending.version = pendingVersion;
            profile = pending;
            Serial.printf("[ContainerMonitor] Container profile version %lu applied (%u rules)\n",
                          static_cast<unsigned long>(profile.version), static_cast<unsigned>(profile.ruleCount));
            evaluate(true);
        }
    }

protected:
    /** @runs_on{network} */
    void processQueuedTelemetryData(const TelemetryPackage* package) const override {
        if (package == nullptr) return;
        const auto* record = static_cast<const EdgeRecord*>(package); // Every package of this node is an EdgeRecord.
        JsonDocument json;
        record->serialize(json);
        char body[320];
        serializeJson(json, body, sizeof(body));
        // The client keeps the pointer to the URL, so each route has its own buffer.
        edge.setServiceEndpointUrl(record->isActuationEvent() ? actuationEventsUrl : readingsUrl);
        if (edge.sendRequest(HttpGatewayClient::HttpMethod::Post, body)) {
            lastContactAt = millis();
            return;
        }
        const int status = edge.getLastResponseStatusCode();
        if (status == 401) signedIn = false;
        Serial.printf("[ContainerMonitor] Record not delivered (status %d): %s\n", status, body);
    }

    /** @runs_on{network} */
    void maintainNetworkServices() const override {
        if (!wifi.isConnected()) return;
        const unsigned long now = millis();
        if (!TimeSynchronizationDriver::isSynchronized() && due(lastClockAttemptAt, now, RETRY_INTERVAL)) {
            // The driver marks the clock as synchronized only inside initialize(), so it is retried.
            TimeSynchronizationDriver::initialize(TIME_ZONE, NTP_SERVER);
            if (TimeSynchronizationDriver::isSynchronized()) Serial.println("[ContainerMonitor] Clock synchronized");
        }
        if (!signedIn && due(lastSignInAttemptAt, now, RETRY_INTERVAL)) signIn();
        if (due(lastConfigurationAttemptAt, now, configurationRead ? 300000 : RETRY_INTERVAL)) readConfiguration();
    }

private:
    static constexpr unsigned long SAMPLER_TICK_IN_MILLISECONDS = 100;
    static constexpr uint32_t SAMPLER_STACK_BYTES = 6144;
    static constexpr uint8_t QUEUE_LENGTH = 32;
    static constexpr uint8_t SENSOR_FAULT_THRESHOLD = 3;
    static constexpr int LIGHT_SENSOR_IDENTIFIER = 0x23; // The BH1750 has no signal pin: it is on the I2C bus.
    // The ServoMotor sends a pulse of angle / 180 of its 20 ms period, while an SG90 takes 0.5-2.4 ms for 0-180 degrees,
    // so these "angles" are the pulses of the latch: 5 gives about 0.55 ms (closed, about 4 degrees) and 13 about 1.44 ms
    // (open, about 89 degrees).
    static constexpr int LOCK_CLOSED_ANGLE = 5, LOCK_OPEN_ANGLE = 13;
    static constexpr unsigned long OPENING_DURATION = 10000;  // Lock open after an authorized tag.
    static constexpr unsigned long REVIEW_TIMEOUT = 300000;   // Without the Edge, the node needs review.
    static constexpr unsigned long RETRY_INTERVAL = 30000;
    static constexpr int EVALUATE_EVENT = 1000, ANIMATE_EVENT = 1001, REPORT_EVENT = 1002, ALARM_REPEAT_EVENT = 1003;
    static constexpr int BEGIN_PROFILE = 2000, SET_LIMIT = 2001, ADD_RULE = 2013, APPLY_PROFILE = 2014;
    static constexpr int32_t NO_LIMIT = INT32_MIN;

    enum class LedPattern { Normal, Warning, Critical, NeedsReview, Linking };

    ClimateSensor climateSensor;
    DigitalLightIntensitySensor lightSensor;
    RfidSensor tagReader;
    ServoMotor lockServo;
    RelayActuator fan;
    RgbLed statusLed;
    Buzzer alarmBuzzer;
    ContainerDisplay display;
    WiFiConnectivityDriver& wifi;
    HttpGatewayClient& edge;
    char signInUrl[128]{}, readingsUrl[128]{}, actuationEventsUrl[128]{}, configurationUrl[160]{};

    // Sampler task state.
    Profile profile, pending;
    uint32_t pendingVersion = 0;
    bool receiving = false;
    float values[METRIC_COUNT] = {NAN, NAN, NAN};
    State states[METRIC_COUNT] = {State::Normal, State::Normal, State::Normal};
    bool faulted[METRIC_COUNT] = {false, false, false};
    State overall = State::Normal;
    bool actionOn[ACTION_COUNT] = {false, false, false};
    Metric actionCause[ACTION_COUNT] = {Metric::Temperature, Metric::Temperature, Metric::Temperature};
    bool lockOpen = false, openedByTag = false, alarmAcknowledged = false;
    unsigned long openedByTagAt = 0;
    bool accessFeedback = false, accessGranted = false;
    unsigned long accessFeedbackAt = 0;
    bool ledDrawn = false, ledLit = false;
    RgbColor ledColor{0, 0, 0};
    char serialTag[32]{};
    size_t serialTagLength = 0;

    // Network task state (its hooks are const in the framework); the sampler reads the atomics.
    mutable std::atomic<bool> signedIn{false};
    mutable std::atomic<unsigned long> lastContactAt{0};
    mutable bool configurationRead = false;
    mutable unsigned long lastClockAttemptAt = 0, lastSignInAttemptAt = 0, lastConfigurationAttemptAt = 0;

    static bool due(unsigned long& lastAttemptAt, unsigned long now, unsigned long interval) {
        if (lastAttemptAt != 0 && now - lastAttemptAt < interval) return false;
        lastAttemptAt = now == 0 ? 1 : now;
        return true;
    }

    /** @brief Reads the sensors, classifies each metric, applies the rules and draws the screen. */
    void evaluate(bool profileChanged) {
        values[0] = climateSensor.getTemperatureInCelsius(); // NaN until the first reading.
        values[1] = climateSensor.getRelativeHumidityInPercentage();
        values[2] = lightSensor.hasValidSample() ? lightSensor.getIlluminanceInLux() : NAN;

        const State previousOverall = overall;
        bool changed = false;
        overall = State::Normal;
        for (size_t i = 0; i < METRIC_COUNT; i++) {
            const Threshold& threshold = profile.thresholds[i];
            State next = State::Normal;
            if (threshold.isDefined() && !faulted[i] && !std::isnan(values[i])) {
                // A new profile classifies at once; otherwise a better state needs the hysteresis margin (MS10).
                next = profileChanged ? threshold.classify(values[i]) : threshold.evaluate(values[i], states[i], HYSTERESIS[i]);
            }
            if (next != states[i]) {
                Serial.printf("[ContainerMonitor] %s %.1f: %s -> %s\n", Names::of(static_cast<Metric>(i)), values[i],
                              Names::of(states[i]), Names::of(next));
                states[i] = next;
                changed = true;
            }
            if (next > overall) overall = next;
        }
        if (changed) {
            soundStateChange(previousOverall);
            sendClimate(); // The Edge learns about the change at once.
        }
        applyRules();
        refreshScreen(millis());
    }

    void applyRules() {
        bool wanted[ACTION_COUNT] = {false, false, false};
        Metric cause[ACTION_COUNT] = {Metric::Temperature, Metric::Temperature, Metric::Temperature};
        for (size_t r = 0; r < profile.ruleCount; r++) {
            const Rule& rule = profile.rules[r];
            const int action = static_cast<int>(rule.action);
            if (!wanted[action] && states[static_cast<int>(rule.metric)] >= rule.state) {
                wanted[action] = true;
                cause[action] = rule.metric;
            }
        }
        for (size_t a = 0; a < ACTION_COUNT; a++) setAction(static_cast<Action>(a), wanted[a], cause[a]);
    }

    void setAction(Action action, bool on, Metric metric) {
        const int index = static_cast<int>(action);
        if (actionOn[index] == on) return;
        if (on) actionCause[index] = metric;
        else metric = actionCause[index]; // The action stops because its metric got better.
        actionOn[index] = on;

        if (action == Action::ServoOpen) {
            updateLock(true, metric);
            return;
        }
        // Ventilation and simulated cooling share the fan of the container.
        const bool fanOn = actionOn[static_cast<int>(Action::Ventilation)] || actionOn[static_cast<int>(Action::Cooling)];
        fan.handle(fanOn ? RelayActuator::RELAY_ON_COMMAND : RelayActuator::RELAY_OFF_COMMAND);
        const bool executed = (digitalRead(FAN_PIN) == HIGH) == fanOn;
        Serial.printf("[ContainerMonitor] %s (%s %s)%s\n", Names::of(action, on), Names::of(metric),
                      Names::of(states[static_cast<int>(metric)]), executed ? "" : " FAILED");
        sendAction(action, on, true, metric, executed);
    }

    void updateLock(bool causedByRule, Metric metric) {
        const bool open = openedByTag || actionOn[static_cast<int>(Action::ServoOpen)];
        if (open == lockOpen) return;
        const int angle = open ? LOCK_OPEN_ANGLE : LOCK_CLOSED_ANGLE;
        lockServo.handle(Command{ServoMotor::DRIVE_TO_ANGLE_COMMAND_IDENTIFIER, angle});
        const bool executed = lockServo.getCurrentActiveAngleInDegrees() == angle;
        if (executed) lockOpen = open;
        Serial.printf("[ContainerMonitor] %s%s\n", Names::of(Action::ServoOpen, open), executed ? "" : " FAILED");
        sendAction(Action::ServoOpen, open, causedByRule, metric, executed);
    }

    void animate() {
        if (SERIAL_TAG_INPUT) readSerialTag();
        // Read after the tags: a tag read above stores a later millis(), and the unsigned difference with an earlier
        // one would end the opening at once.
        const unsigned long now = millis();
        if (openedByTag && now - openedByTagAt >= OPENING_DURATION) {
            openedByTag = false;
            Serial.println("[ContainerMonitor] Opening time over");
            updateLock(false, Metric::Temperature);
        }
        animateLed(now);
    }

    void onTag(const char* tag) {
        if (tag == nullptr || tag[0] == '\0') return;
        const unsigned long now = millis();
        bool granted = false;
        for (const char* authorized : AUTHORIZED_TAGS) granted = granted || strcasecmp(authorized, tag) == 0;
        Serial.printf("[ContainerMonitor] Tag %s: access %s\n", tag, granted ? "granted" : "denied");
        if (TimeSynchronizationDriver::isSynchronized()) {
            enqueueTelemetryPayload(ContainerReading::rfidTag(TimeSynchronizationDriver::getCurrentTimestamp(), tag,
                                                              profile.version));
        }

        display.showAccess(granted, tag, now);
        accessFeedback = true;
        accessGranted = granted;
        accessFeedbackAt = now;
        if (granted) {
            if (overall == State::Critical && !alarmAcknowledged) {
                alarmAcknowledged = true; // US74: the staff acknowledges the local alarm.
                Serial.printf("[ContainerMonitor] Alarm acknowledged by %s\n", tag);
            }
            playTone(2500, 120);
            openedByTag = true;
            openedByTagAt = now; // A new tag while open restarts the opening time.
            updateLock(false, Metric::Temperature);
        } else {
            playTone(400, 800);
        }
        animateLed(now);
        refreshScreen(now);
    }

    /** @brief Reads the UIDs typed on the serial monitor, one per line (Wokwi has no RC522 in this circuit). */
    void readSerialTag() {
        while (Serial.available() > 0) {
            const int character = Serial.read();
            if (character == '\n' || character == '\r') {
                if (serialTagLength == 0) continue;
                serialTag[serialTagLength] = '\0';
                serialTagLength = 0;
                onTag(serialTag);
            } else if (character > ' ' && serialTagLength < sizeof(serialTag) - 1) {
                serialTag[serialTagLength++] = static_cast<char>(toupper(character));
            }
        }
    }

    void onSensorFault(int sourceId, bool fault) {
        if (sourceId == climateSensor.getPin()) {
            faulted[0] = faulted[1] = fault;
            Serial.printf("[ContainerMonitor] DHT %s\n", fault ? "fault" : "recovered");
        } else if (sourceId == LIGHT_SENSOR_IDENTIFIER) {
            faulted[2] = fault;
            if (fault) lightSensor.initialize(); // Set the BH1750 up again in case it was reconnected.
            Serial.printf("[ContainerMonitor] BH1750 %s\n", fault ? "fault" : "recovered");
        }
    }

    void soundStateChange(State previousOverall) {
        if (overall != State::Critical) alarmAcknowledged = false; // A new CRITICAL needs a new acknowledgement.
        if (overall <= previousOverall) {
            if (overall == State::Normal) alarmBuzzer.handle(Buzzer::TURN_OFF_COMMAND);
            return;
        }
        if (overall == State::Critical) playTone(2000, 500);
        else playTone(1000, 200);
    }

    void playTone(int frequencyInHertz, unsigned long durationInMilliseconds) {
        alarmBuzzer.setTone(frequencyInHertz, durationInMilliseconds);
        alarmBuzzer.handle(Buzzer::PLAY_TONE_COMMAND);
    }

    bool edgeSilent() const { return millis() - lastContactAt.load() >= REVIEW_TIMEOUT; }

    bool needsReview() const { return faulted[0] || faulted[1] || faulted[2] || edgeSilent(); }

    const char* linkLabel() const {
        if (edgeSilent()) return "NO EDGE";
        return signedIn ? "ONLINE" : "LINKING";
    }

    void refreshScreen(unsigned long now) {
        const bool ventilation = actionOn[static_cast<int>(Action::Ventilation)];
        const bool cooling = actionOn[static_cast<int>(Action::Cooling)];
        ContainerSnapshot snapshot{};
        snapshot.deviceId = DEVICE_ID;
        snapshot.overallState = Names::of(overall);
        MetricLine* lines[] = {&snapshot.temperature, &snapshot.humidity, &snapshot.luminosity};
        for (size_t i = 0; i < METRIC_COUNT; i++) {
            *lines[i] = {faulted[i] ? NAN : values[i], faulted[i] ? "FAULT" : Names::of(states[i])};
        }
        snapshot.action = ventilation && cooling ? "VENT+COOL" : ventilation ? "VENTILATION" : cooling ? "COOLING" : "AUTO";
        snapshot.locked = !lockOpen;
        snapshot.link = linkLabel();
        snapshot.alarmAcknowledged = alarmAcknowledged;
        display.refresh(snapshot, now);
    }

    /**
     * @brief Status LED: green slow pulse NORMAL, yellow blink WARNING, red fast blink CRITICAL, solid red needs
     * review, blue pulse linking; over them, green 2 s for an access granted and 3 red flashes for one denied.
     */
    void animateLed(unsigned long now) {
        if (accessFeedback) {
            const unsigned long elapsed = now - accessFeedbackAt;
            if (accessGranted && elapsed < 2000) {
                drawLed(RgbLed::GREEN, true);
                return;
            }
            if (!accessGranted && elapsed < 1500) {
                drawLed(RgbLed::RED, blink(elapsed, 500));
                return;
            }
            accessFeedback = false;
        }
        LedPattern pattern = LedPattern::Normal;
        if (overall == State::Critical) pattern = LedPattern::Critical;
        else if (needsReview()) pattern = LedPattern::NeedsReview;
        else if (overall == State::Warning) pattern = LedPattern::Warning;
        else if (!signedIn) pattern = LedPattern::Linking;

        switch (pattern) {
            case LedPattern::Warning: drawLed(RgbLed::YELLOW, blink(now, 1600)); break;
            case LedPattern::Critical: drawLed(RgbLed::RED, blink(now, 400)); break;
            case LedPattern::NeedsReview: drawLed(RgbLed::RED, true); break;
            case LedPattern::Linking: drawLed(pulse(RgbLed::BLUE, now, 2000), true); break;
            case LedPattern::Normal: default: drawLed(pulse(RgbLed::GREEN, now, 3000), true); break;
        }
    }

    static bool blink(unsigned long now, unsigned long period) { return now % period < period / 2; }

    /** @brief The color with a brightness that rises and falls once per period, never below 20/255. */
    static RgbColor pulse(const RgbColor& color, unsigned long now, unsigned long period) {
        const unsigned long phase = now % period, half = period / 2;
        const unsigned long rising = phase < half ? phase : period - phase;
        const unsigned brightness = 20 + 235 * rising / half;
        return RgbColor{static_cast<uint8_t>(color.redIntensity * brightness / 255),
                        static_cast<uint8_t>(color.greenIntensity * brightness / 255),
                        static_cast<uint8_t>(color.blueIntensity * brightness / 255)};
    }

    void drawLed(const RgbColor& color, bool lit) {
        const bool sameColor = color.redIntensity == ledColor.redIntensity &&
                               color.greenIntensity == ledColor.greenIntensity &&
                               color.blueIntensity == ledColor.blueIntensity;
        if (ledDrawn && lit == ledLit && (!lit || sameColor)) return; // Nothing changed: no PWM writes.
        ledDrawn = true;
        ledLit = lit;
        ledColor = color;
        if (lit) {
            statusLed.setColor(color);
            statusLed.handle(RgbLed::SET_COLOR_COMMAND);
        } else {
            statusLed.handle(RgbLed::TURN_OFF_COMMAND);
        }
    }

    /** @brief The Edge needs the UTC time of each observation, so records wait for the NTP clock. */
    void sendClimate() {
        float reported[METRIC_COUNT];
        bool hasValue = false;
        for (size_t i = 0; i < METRIC_COUNT; i++) {
            reported[i] = faulted[i] ? NAN : values[i];
            hasValue = hasValue || !std::isnan(reported[i]);
        }
        if (!hasValue || !TimeSynchronizationDriver::isSynchronized()) return;
        enqueueTelemetryPayload(ContainerReading::climate(TimeSynchronizationDriver::getCurrentTimestamp(), reported[0],
                                                          reported[1], reported[2], profile.version));
    }

    void sendAction(Action action, bool on, bool hasCause, Metric metric, bool executed) {
        if (!TimeSynchronizationDriver::isSynchronized()) return;
        enqueueTelemetryPayload(std::unique_ptr<ActuationEvent>(new ActuationEvent(
            TimeSynchronizationDriver::getCurrentTimestamp(), action, on, hasCause, metric,
            states[static_cast<int>(metric)], executed, profile.version)));
    }

    /** @runs_on{network} */
    void signIn() const {
        char body[64];
        snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", DEVICE_ID);
        edge.setServiceEndpointUrl(signInUrl);
        signedIn = edge.sendRequest(HttpGatewayClient::HttpMethod::Post, body);
        const int status = edge.getLastResponseStatusCode();
        if (signedIn) {
            lastContactAt = millis();
            Serial.printf("[ContainerMonitor] Edge accepted %s\n", DEVICE_ID);
        } else if (status == 401) {
            Serial.println("[ContainerMonitor] Edge rejected the node: check DEVICE_ID and DEVICE_API_KEY");
        } else {
            Serial.printf("[ContainerMonitor] Edge not reachable at %s (status %d)\n", EDGE_BASE_URL, status);
        }
    }

    /**
     * @brief Reads the configuration of the node and hands its profile to the sampler task.
     *
     * The `HttpGatewayClient` keeps 511 bytes of a response and the configuration of a container is longer (three
     * thresholds and the rules), so this request reads the body with the `HTTPClient` of the ESP32 core.
     * @runs_on{network}
     */
    void readConfiguration() const {
        WiFiClient socket;
        HTTPClient http;
        if (!http.begin(socket, configurationUrl)) return;
        http.setTimeout(5000);
        http.addHeader("Authorization", authorizationHeader);
        const int status = http.GET();
        const String body = status == 200 ? http.getString() : String();
        http.end();
        if (status == 404) {
            lastContactAt = millis();
            Serial.println("[ContainerMonitor] The Edge has no profile of the cloud for this node yet");
            return;
        }
        if (status != 200) {
            if (status == 401) signedIn = false;
            Serial.printf("[ContainerMonitor] Configuration not read (status %d)\n", status);
            return;
        }
        lastContactAt = millis();

        JsonDocument filter;
        filter["device_type"] = true;
        filter["profile_version"] = true;
        for (const char* key : {"metric", "normal_min", "normal_max", "critical_min", "critical_max"}) {
            filter["thresholds"][0][key] = true;
        }
        for (const char* key : {"metric", "state", "action"}) filter["actuation_rules"][0][key] = true;
        JsonDocument configuration;
        if (deserializeJson(configuration, body, DeserializationOption::Filter(filter))) {
            Serial.println("[ContainerMonitor] Configuration not understood");
            return;
        }
        Profile edgeProfile;
        if (!translate(configuration, edgeProfile)) {
            Serial.println("[ContainerMonitor] The configuration has no valid container profile");
            return;
        }
        configurationRead = enqueueProfile(edgeProfile);
    }

    static bool translate(const JsonDocument& configuration, Profile& destination) {
        if (strcmp(configuration["device_type"] | "", "CONTAINER_MONITOR") != 0) return false;
        const uint32_t version = configuration["profile_version"] | 0u;
        if (version == 0) return false;
        Profile edgeProfile;
        edgeProfile.version = version;
        bool hasThreshold = false;
        for (JsonObjectConst threshold : configuration["thresholds"].as<JsonArrayConst>()) {
            const int metric = Names::indexIn(METRIC_NAMES, METRIC_COUNT, threshold["metric"] | "");
            if (metric < 0) continue; // RFID_TAG has no limits.
            const Threshold limits{threshold["normal_min"] | NAN, threshold["normal_max"] | NAN,
                                   threshold["critical_min"] | NAN, threshold["critical_max"] | NAN};
            if (!limits.isValid()) return false;
            edgeProfile.thresholds[metric] = limits;
            hasThreshold = true;
        }
        if (!hasThreshold) return false;
        for (JsonObjectConst rule : configuration["actuation_rules"].as<JsonArrayConst>()) {
            const int metric = Names::indexIn(METRIC_NAMES, METRIC_COUNT, rule["metric"] | "");
            const int state = Names::indexIn(STATE_NAMES, 3, rule["state"] | "");
            const int action = Names::indexIn(ACTION_ON_NAMES, ACTION_COUNT, rule["action"] | "");
            if (metric < 0 || state < 0 || action < 0) return false;
            if (!edgeProfile.addRule({static_cast<Metric>(metric), static_cast<State>(state), static_cast<Action>(action)})) {
                return false;
            }
        }
        destination = edgeProfile;
        return true;
    }

    /**
     * @brief The framework's hooks are const; enqueueCommand() is how the network task hands data to the sampler.
     * @return False when the command queue was full; the profile is read again in 30 seconds.
     */
    bool enqueueProfile(const Profile& edgeProfile) const {
        auto* device = const_cast<ContainerMonitor*>(this);
        bool queued = device->enqueueCommand(device, Command{BEGIN_PROFILE, static_cast<int32_t>(edgeProfile.version)});
        for (size_t m = 0; queued && m < METRIC_COUNT; m++) {
            const Threshold& t = edgeProfile.thresholds[m];
            const float limits[] = {t.normalMin, t.normalMax, t.criticalMin, t.criticalMax};
            for (int i = 0; queued && i < 4; i++) {
                const int32_t payload = std::isnan(limits[i]) ? NO_LIMIT : static_cast<int32_t>(lroundf(limits[i] * 100));
                queued = device->enqueueCommand(device, Command{SET_LIMIT + static_cast<int>(m) * 4 + i, payload});
            }
        }
        for (size_t r = 0; queued && r < edgeProfile.ruleCount; r++) {
            const Rule& rule = edgeProfile.rules[r];
            const int32_t payload = static_cast<int32_t>(rule.metric) | static_cast<int32_t>(rule.state) << 8 |
                                    static_cast<int32_t>(rule.action) << 16;
            queued = device->enqueueCommand(device, Command{ADD_RULE, payload});
        }
        queued = queued && device->enqueueCommand(device, Command{APPLY_PROFILE, static_cast<int32_t>(edgeProfile.version)});
        if (!queued) Serial.println("[ContainerMonitor] Command queue full; the profile is read again later");
        return queued;
    }

};

static WiFiConnectivityDriver* transportDriver = nullptr;
static HttpGatewayClient* edgeGateway = nullptr;
static ContainerMonitor* containerMonitor = nullptr;

void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    Serial.printf("\n[ContainerMonitor] %s starting\n", DEVICE_ID);

    // Shared I2C bus of the OLED and the BH1750; the OLED is set up when the device is created.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_IN_HERTZ);

    transportDriver = new WiFiConnectivityDriver(WIFI_SSID, WIFI_PASSWORD);
    transportDriver->connect(); // The driver keeps reconnecting in the background.

    snprintf(authorizationHeader, sizeof(authorizationHeader), "Bearer %s", DEVICE_API_KEY);
    edgeGateway = new HttpGatewayClient(*transportDriver, EDGE_BASE_URL);
    edgeGateway->setAuthorizationHeader(authorizationHeader);

    containerMonitor = new ContainerMonitor(*transportDriver, *edgeGateway);
}

void loop() {
    // The sampler and network tasks of the framework run the node; the loop only yields.
    vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_IN_MILLISECONDS));
}
