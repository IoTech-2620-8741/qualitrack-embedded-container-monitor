/**
 * @file ContainerDisplay.h
 * @brief Screen of the Container Monitor on the 0.96" SSD1306 OLED of the frame (MS08, US71).
 *
 * The screen follows this order:
 *
 * 1. Identifier of the container and its overall state.
 * 2. Temperature, humidity and luminosity, each one with its state.
 * 3. Active action ("AUTO" when no rule acts) and state of the lock.
 * 4. Connectivity with the Edge ("ONLINE", "LINKING" or "NO EDGE").
 *
 * When a tag is presented, the screen shows ACCESS GRANTED or ACCESS DENIED for 3 seconds. The font
 * is ASCII, 6 px per character: a line holds 21 characters.
 *
 * The `OledDisplay` of the framework v1.0.0 sends each argument of a command in its own I2C
 * transaction and writes pages with page-addressing commands while the panel is in horizontal mode;
 * in Wokwi every page lands on the first two. This class draws into its own frame buffer and sends
 * it the usual way: the whole setup in one transaction, then a drawing window and the data in small
 * transactions that fit the I2C buffer of the ESP32.
 */
#ifndef CONTAINER_DISPLAY_H
#define CONTAINER_DISPLAY_H

#include <Arduino.h>
#include "ContainerProfile.h"

/** @brief Link of the node with the Edge, as the screen shows it. */
enum class EdgeLink : uint8_t {
    Linking, ///< The Edge has not accepted the node yet.
    Online,  ///< The Edge accepted the node and answers.
    Offline  ///< The Edge has not answered for the review timeout.
};

/** @brief Data the screen shows; a NaN value is shown as missing. */
struct ContainerSnapshot {
    const char* deviceId;
    float values[CONTAINER_METRIC_COUNT];
    EnvironmentalState states[CONTAINER_METRIC_COUNT];
    bool faulted[CONTAINER_METRIC_COUNT];
    EnvironmentalState overallState;
    bool locked;
    bool ventilationOn;
    bool coolingOn;
    bool alarmAcknowledged;
    EdgeLink link;
};

/**
 * @brief Draws the container data on the OLED, sending a frame only when the text changes.
 * @note Called only from the sampler task; the I2C bus must be started before initialize().
 */
class ContainerDisplay {
public:
    /// Time the access result stays on the screen.
    static constexpr unsigned long ACCESS_MESSAGE_DURATION_IN_MILLISECONDS = 3000;
    /// I2C address of the OLED module.
    static constexpr uint8_t DEFAULT_I2C_ADDRESS = 0x3C;

    /** @param i2cAddress I2C address of the OLED. */
    explicit ContainerDisplay(uint8_t i2cAddress = DEFAULT_I2C_ADDRESS);

    /**
     * @brief Sets the panel up and clears it.
     * @return False when the OLED does not answer.
     */
    bool initialize();

    /**
     * @brief Shows the result of a tag over the data for 3 seconds.
     * @param granted True for an authorized tag.
     * @param tag UID of the tag.
     * @param nowInMilliseconds Current millis().
     */
    void showAccess(bool granted, const char* tag, unsigned long nowInMilliseconds);

    /**
     * @brief Draws the data, or the access result while it lasts.
     * @param snapshot Current data of the container.
     * @param nowInMilliseconds Current millis().
     */
    void refresh(const ContainerSnapshot& snapshot, unsigned long nowInMilliseconds);

private:
    static constexpr uint8_t WIDTH = 128;
    static constexpr uint8_t HEIGHT = 64;
    static constexpr size_t LINE_COUNT = 6;
    static constexpr size_t LINE_SIZE = 22; // 21 characters of 6 px on 128 px, and the terminator.

    uint8_t i2cAddress;
    uint8_t frame[WIDTH * HEIGHT / 8];
    char lines[LINE_COUNT][LINE_SIZE];
    char shownLines[LINE_COUNT][LINE_SIZE];
    bool accessShown;
    bool accessGranted;
    char accessTag[LINE_SIZE];
    unsigned long accessShownAt;
    bool framed;
    bool drawn;

    void composeData(const ContainerSnapshot& snapshot);
    void composeAccess();
    void composeMetric(size_t line, const char* label, const char* format, const ContainerSnapshot& snapshot,
                       ContainerMetric metric);
    void draw();
    void setPixel(uint8_t x, uint8_t y);
    void drawText(const char* text, uint8_t x, uint8_t y);
    void drawHorizontalLine(uint8_t x, uint8_t y, uint8_t length);
    void drawRectangle(uint8_t x, uint8_t y, uint8_t width, uint8_t height);
    void sendFrame() const;
    static const char* stateLabelOf(EnvironmentalState state);
};

#endif // CONTAINER_DISPLAY_H
