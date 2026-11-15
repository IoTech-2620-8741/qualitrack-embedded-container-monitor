/**
 * @file StatusIndicator.h
 * @brief Patterns of the status LED of the Container Monitor (RGB LED on the top frame).
 *
 * | Status | Color | Pattern |
 * |--------|-------|---------|
 * | Normal operation (NORMAL) | Green | Slow pulse |
 * | Warning (WARNING) | Yellow | Slow blink |
 * | Critical alert (CRITICAL) | Red | Fast blink |
 * | Needs review (no Edge for 5 min or a sensor fault) | Red | Solid |
 * | Access granted (authorized tag) | Green | Solid for 2 s |
 * | Access denied (unknown tag) | Red | 3 flashes |
 * | Linking with the Edge | Blue | Soft pulse |
 *
 * The access feedback patterns are temporary: when they end, the LED goes back to the status pattern.
 */
#ifndef STATUS_INDICATOR_H
#define STATUS_INDICATOR_H

#include <ModestIoT.h>

/** @brief Pattern the status LED shows. */
enum class LedPattern : uint8_t {
    Normal,
    Warning,
    Critical,
    NeedsReview,
    Linking,
    AccessGranted,
    AccessDenied
};

/**
 * @brief Animates the RGB LED of the framework with the status patterns.
 * @note Called only from the sampler task.
 */
class StatusIndicator {
public:
    /// Duration of the access granted pattern.
    static constexpr unsigned long ACCESS_GRANTED_DURATION_IN_MILLISECONDS = 2000;
    /// Duration of one flash (on and off) of the access denied pattern.
    static constexpr unsigned long ACCESS_DENIED_FLASH_IN_MILLISECONDS = 500;
    /// Flashes of the access denied pattern.
    static constexpr uint8_t ACCESS_DENIED_FLASH_COUNT = 3;

    /**
     * @param led RGB LED of the device.
     */
    explicit StatusIndicator(RgbLed& led);

    /**
     * @brief Sets the lasting pattern of the node status.
     * @param pattern Normal, Warning, Critical, NeedsReview or Linking.
     */
    void showStatus(LedPattern pattern);

    /**
     * @brief Shows a temporary pattern (AccessGranted or AccessDenied) over the status pattern.
     * @param pattern The feedback pattern.
     * @param nowInMilliseconds Current millis().
     */
    void showFeedback(LedPattern pattern, unsigned long nowInMilliseconds);

    /**
     * @brief Draws the current frame of the pattern; call it every tick of the sampler.
     * @param nowInMilliseconds Current millis().
     */
    void refresh(unsigned long nowInMilliseconds);

    /** @return True while a temporary pattern is shown. */
    [[nodiscard]] bool isShowingFeedback() const;

private:
    RgbLed& led;
    LedPattern statusPattern;
    LedPattern feedbackPattern;
    bool feedbackActive;
    unsigned long feedbackStartedAt;
    RgbColor lastColor;
    bool lastLit;
    bool hasDrawn;

    /** @return Brightness (0-255) of a triangle wave of the given period. */
    static uint8_t pulseOf(unsigned long nowInMilliseconds, unsigned long periodInMilliseconds, uint8_t floor);
    /** @return True during the first half of each period. */
    static bool blinkOf(unsigned long nowInMilliseconds, unsigned long periodInMilliseconds);
    static RgbColor scaled(const RgbColor& color, uint8_t brightness);
    void draw(const RgbColor& color, bool lit);
};

#endif // STATUS_INDICATOR_H
