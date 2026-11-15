/**
 * @file ContainerMonitorDevice.h
 * @brief Mediator of the QualiTrack Container Monitor (one ESP32 per container of drug lots).
 *
 * Following the Modest IoT Nano Framework, the device owns its sensors and actuators and reacts to
 * their events on the sampler task:
 *
 * | Story | Behavior |
 * |-------|----------|
 * | MS06 | Reads the temperature and humidity of the container (DHT) |
 * | MS07 | Reads the luminosity of the container (BH1750) |
 * | MS08 | Shows the data, the lock, the action and the Edge link on the OLED |
 * | MS09 | Reads the RFID tag of the person who opens the container (RC522) |
 * | MS10 | Starts and stops the ventilation or simulated cooling with the rules of the profile and hysteresis |
 * | MS11 | Opens the lock for an authorized tag and closes it when the opening time ends |
 * | MS12 | Sends the climate every minute and at once when a state changes, and every tag read |
 * | MS13 | Sends every action it executes, with its cause and its result |
 * | MS15 | Applies the profile (thresholds, rules and version) the Edge hands it |
 * | US71 | Lets the staff check the data on the screen and the status LED |
 * | US74 | Lets the staff acknowledge the local alarm with an authorized tag |
 *
 * The local reaction does not depend on the network: with no Edge or no profile yet, the node keeps
 * evaluating the readings with its default profile.
 */
#ifndef CONTAINER_MONITOR_DEVICE_H
#define CONTAINER_MONITOR_DEVICE_H

#include <ModestIoT.h>
#include "ClimateSensor.h"
#include "ContainerDisplay.h"
#include "ContainerProfile.h"
#include "EdgeGatewayClient.h"
#include "LockServo.h"
#include "StatusIndicator.h"

/**
 * @brief Hands the profile read on the network task to the sampler task.
 *
 * The framework workers share no state but the telemetry queue; this small mailbox, guarded by a
 * FreeRTOS critical section, is the only other value they exchange.
 */
class ProfileMailbox {
public:
    /** @brief Leaves a profile for the sampler task (network task). */
    void post(const ContainerProfile& profile);

    /**
     * @brief Takes the last profile left, if any (sampler task).
     * @param destination Receives the profile.
     * @return True when there was a new profile.
     */
    bool take(ContainerProfile& destination);

private:
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    ContainerProfile pendingProfile;
    bool hasPendingProfile = false;
};

/** @brief Pins of the circuit of the Container Monitor. */
struct ContainerMonitorPins {
    int climateSensor;     ///< DHT data.
    int tagReaderSelect;   ///< RC522 SDA (SPI chip select).
    int tagReaderReset;    ///< RC522 RST.
    int lockServo;         ///< SG90 signal.
    int fan;               ///< Base of the 2N2222 of the fan.
    int ledRed;            ///< RGB LED red channel.
    int ledGreen;          ///< RGB LED green channel.
    int ledBlue;           ///< RGB LED blue channel.
    int buzzer;            ///< Buzzer.
};

/** @brief Settings of one Container Monitor node. */
struct ContainerMonitorSettings {
    const char* deviceId;               ///< Identifier shown on the screen.
    uint8_t climateSensorModel;         ///< ClimateSensor::MODEL_DHT11 or MODEL_DHT22.
    const char* const* authorizedTags;  ///< UIDs of the tags of the authorized staff ("DE:AD:BE:EF").
    size_t authorizedTagCount;          ///< Number of authorized tags.
    bool serialTagInput;                ///< Also read tags typed on the serial monitor (Wokwi has no RC522).
};

/**
 * @brief Container Monitor: climate, access and local actions of a container of drug lots.
 */
class ContainerMonitorDevice : public Device, public EdgeConfigurationListener {
public:
    /// Tick of the sampler, in milliseconds.
    static constexpr unsigned long SAMPLER_TICK_IN_MILLISECONDS = 100;
    /// Depth of the telemetry and command queues.
    static constexpr uint8_t QUEUE_LENGTH = 16;

    /// Event of the scheduler that evaluates the metrics, the rules and the screen.
    static constexpr int EVALUATION_EVENT_IDENTIFIER = 1000;
    static constexpr uint32_t EVALUATION_INTERVAL_IN_MILLISECONDS = 1000;
    /// Event of the scheduler that animates the LED, reads the serial tags and ends the opening.
    static constexpr int ANIMATION_EVENT_IDENTIFIER = 1001;
    static constexpr uint32_t ANIMATION_INTERVAL_IN_MILLISECONDS = 100;
    /// Event of the scheduler that sends the climate even when it does not change (MS12).
    static constexpr int REPORT_EVENT_IDENTIFIER = 1002;
    static constexpr uint32_t REPORT_INTERVAL_IN_MILLISECONDS = 60000;
    /// Event of the scheduler that repeats the alarm while the container is CRITICAL.
    static constexpr int ALARM_REPEAT_EVENT_IDENTIFIER = 1003;
    static constexpr uint32_t ALARM_REPEAT_INTERVAL_IN_MILLISECONDS = 2000;

    /// Time without an answer of the Edge (or a sensor reading) before the node needs review.
    static constexpr unsigned long REVIEW_TIMEOUT_IN_MILLISECONDS = 300000;
    /// Time the lock stays open after an authorized tag.
    static constexpr unsigned long OPENING_DURATION_IN_MILLISECONDS = 10000;
    /// Servo angle of the closed and the open lock.
    static constexpr int LOCK_CLOSED_ANGLE_IN_DEGREES = 0;
    static constexpr int LOCK_OPEN_ANGLE_IN_DEGREES = 90;
    /// Consecutive failed reads before a sensor reports a fault.
    static constexpr uint8_t SENSOR_FAULT_THRESHOLD = 3;
    /// Identifier of the BH1750 in the events (it has no signal pin: it is on the I2C bus).
    static constexpr int LIGHT_SENSOR_IDENTIFIER = 0x23;
    /// I2C address of the OLED.
    static constexpr uint8_t DISPLAY_I2C_ADDRESS = 0x3C;

    /** @name Tones of the buzzer */
    //@{
    static constexpr int WARNING_TONE_FREQUENCY_IN_HERTZ = 1000;
    static constexpr unsigned long WARNING_TONE_DURATION_IN_MILLISECONDS = 200;
    static constexpr int CRITICAL_TONE_FREQUENCY_IN_HERTZ = 2000;
    static constexpr unsigned long CRITICAL_TONE_DURATION_IN_MILLISECONDS = 500;
    static constexpr int GRANTED_TONE_FREQUENCY_IN_HERTZ = 2500;
    static constexpr unsigned long GRANTED_TONE_DURATION_IN_MILLISECONDS = 120;
    static constexpr int DENIED_TONE_FREQUENCY_IN_HERTZ = 400;
    static constexpr unsigned long DENIED_TONE_DURATION_IN_MILLISECONDS = 800;
    //@}

    /**
     * @brief Creates the device, registers its sensors and starts the asynchronous engine.
     * @param pins Pins of the circuit.
     * @param settings Settings of the node.
     * @param gateway Gateway to the Edge API.
     * @note The I2C bus (Wire) must be started before, because the OLED is set up here.
     */
    ContainerMonitorDevice(const ContainerMonitorPins& pins, const ContainerMonitorSettings& settings,
                           EdgeGatewayClient& gateway);

    ~ContainerMonitorDevice() override;

    /**
     * @brief Reacts to the sensors and to the periodic events of the device.
     * @param event Event raised by a sensor or by the scheduler.
     * @runs_on{sampler}
     */
    void on(Event event) override;

    /** @brief The device receives no commands; its actuators are driven from on(). */
    void handle(Command command) override;

    /**
     * @brief Receives the configuration of the node and leaves its profile for the sampler task.
     * @param configuration Body of the Edge configuration resource.
     * @runs_on{network}
     */
    void onEdgeConfiguration(JsonObjectConst configuration) override;

protected:
    /** @runs_on{network} */
    void processQueuedTelemetryData(const TelemetryPackage* rawQueueItemPayload) const override;

    /** @runs_on{network} */
    void maintainNetworkServices() const override;

private:
    static constexpr size_t SERIAL_TAG_BUFFER_SIZE = 32;

    ContainerMonitorSettings settings;
    int fanPin;
    ClimateSensor climateSensor;
    DigitalLightIntensitySensor lightSensor;
    RfidSensor tagReader;
    LockServo lockServo;
    RelayActuator fan;
    RgbLed statusLed;
    Buzzer alarmBuzzer;
    StatusIndicator indicator;
    ContainerDisplay display;
    EdgeGatewayClient& gatewayClient;
    ProfileMailbox profileMailbox;

    ContainerProfile profile;
    float values[CONTAINER_METRIC_COUNT];
    EnvironmentalState states[CONTAINER_METRIC_COUNT];
    bool faulted[CONTAINER_METRIC_COUNT];
    EnvironmentalState overallState;

    bool responseActive[RESPONSE_ACTION_COUNT];
    ContainerMetric responseMetric[RESPONSE_ACTION_COUNT];
    bool lockOpen;
    bool openedByTag;
    unsigned long openedByTagAt;
    bool alarmAcknowledged;

    char serialTag[SERIAL_TAG_BUFFER_SIZE];
    size_t serialTagLength;

    void evaluate();
    void animate();
    void readMetrics();
    bool applyNewProfile();
    bool classifyMetrics(bool withHysteresis);
    void refreshScreen(unsigned long nowInMilliseconds);
    void applyRules();
    void setResponse(ResponseAction action, bool active, ContainerMetric metric);
    void updateLock(bool causedByRule, ContainerMetric metric);
    void driveFan();
    void onTag(const char* tag);
    [[nodiscard]] bool isAuthorized(const char* tag) const;
    void readSerialTag();
    void onSensorFault(int sourceId, bool fault);
    void soundStateChange(EnvironmentalState previousOverall);
    void updateStatus();
    void reportClimate();
    void reportAction(ResponseAction action, bool activation, bool hasCause, ContainerMetric metric, bool executed);
    [[nodiscard]] bool needsReview() const;
    [[nodiscard]] EdgeLink edgeLink() const;
    void playTone(int frequencyInHertz, unsigned long durationInMilliseconds);
    [[nodiscard]] static size_t indexOf(ContainerMetric metric);
    [[nodiscard]] static size_t indexOf(ResponseAction action);
};

#endif // CONTAINER_MONITOR_DEVICE_H
