/**
 * @file StatusIndicator.cpp
 * @brief Implements the status patterns of the RGB LED.
 */
#include "StatusIndicator.h"

namespace {
    constexpr unsigned long SLOW_PULSE_PERIOD_IN_MILLISECONDS = 3000;
    constexpr unsigned long SOFT_PULSE_PERIOD_IN_MILLISECONDS = 2000;
    constexpr unsigned long SLOW_BLINK_PERIOD_IN_MILLISECONDS = 1600;
    constexpr unsigned long FAST_BLINK_PERIOD_IN_MILLISECONDS = 400;
    constexpr uint8_t PULSE_FLOOR = 20;
    constexpr uint8_t FULL_BRIGHTNESS = 255;
}

StatusIndicator::StatusIndicator(RgbLed& led)
    : led(led),
      statusPattern(LedPattern::Linking),
      feedbackPattern(LedPattern::AccessGranted),
      feedbackActive(false),
      feedbackStartedAt(0),
      lastColor{0, 0, 0},
      lastLit(false),
      hasDrawn(false) {}

void StatusIndicator::showStatus(const LedPattern pattern) {
    statusPattern = pattern;
}

void StatusIndicator::showFeedback(const LedPattern pattern, const unsigned long nowInMilliseconds) {
    feedbackPattern = pattern;
    feedbackActive = true;
    feedbackStartedAt = nowInMilliseconds;
    refresh(nowInMilliseconds);
}

bool StatusIndicator::isShowingFeedback() const {
    return feedbackActive;
}

void StatusIndicator::refresh(const unsigned long nowInMilliseconds) {
    if (feedbackActive) {
        const unsigned long elapsed = nowInMilliseconds - feedbackStartedAt;
        if (feedbackPattern == LedPattern::AccessGranted) {
            if (elapsed < ACCESS_GRANTED_DURATION_IN_MILLISECONDS) {
                draw(RgbLed::GREEN, true);
                return;
            }
        } else if (elapsed < ACCESS_DENIED_FLASH_IN_MILLISECONDS * ACCESS_DENIED_FLASH_COUNT) {
            draw(RgbLed::RED, blinkOf(elapsed, ACCESS_DENIED_FLASH_IN_MILLISECONDS));
            return;
        }
        feedbackActive = false;
    }

    switch (statusPattern) {
        case LedPattern::Warning:
            draw(RgbLed::YELLOW, blinkOf(nowInMilliseconds, SLOW_BLINK_PERIOD_IN_MILLISECONDS));
            break;
        case LedPattern::Critical:
            draw(RgbLed::RED, blinkOf(nowInMilliseconds, FAST_BLINK_PERIOD_IN_MILLISECONDS));
            break;
        case LedPattern::NeedsReview:
            draw(RgbLed::RED, true);
            break;
        case LedPattern::Linking:
            draw(scaled(RgbLed::BLUE, pulseOf(nowInMilliseconds, SOFT_PULSE_PERIOD_IN_MILLISECONDS, PULSE_FLOOR)), true);
            break;
        case LedPattern::Normal:
        default:
            draw(scaled(RgbLed::GREEN, pulseOf(nowInMilliseconds, SLOW_PULSE_PERIOD_IN_MILLISECONDS, PULSE_FLOOR)), true);
            break;
    }
}

uint8_t StatusIndicator::pulseOf(const unsigned long nowInMilliseconds, const unsigned long periodInMilliseconds,
                                 const uint8_t floor) {
    const unsigned long phase = nowInMilliseconds % periodInMilliseconds;
    const unsigned long half = periodInMilliseconds / 2;
    const unsigned long rising = phase < half ? phase : periodInMilliseconds - phase;
    return static_cast<uint8_t>(floor + (FULL_BRIGHTNESS - floor) * rising / half);
}

bool StatusIndicator::blinkOf(const unsigned long nowInMilliseconds, const unsigned long periodInMilliseconds) {
    return (nowInMilliseconds % periodInMilliseconds) < periodInMilliseconds / 2;
}

RgbColor StatusIndicator::scaled(const RgbColor& color, const uint8_t brightness) {
    return RgbColor{
        static_cast<uint8_t>(color.redIntensity * brightness / FULL_BRIGHTNESS),
        static_cast<uint8_t>(color.greenIntensity * brightness / FULL_BRIGHTNESS),
        static_cast<uint8_t>(color.blueIntensity * brightness / FULL_BRIGHTNESS)};
}

void StatusIndicator::draw(const RgbColor& color, const bool lit) {
    const bool sameColor = color.redIntensity == lastColor.redIntensity
        && color.greenIntensity == lastColor.greenIntensity && color.blueIntensity == lastColor.blueIntensity;
    if (hasDrawn && lit == lastLit && (!lit || sameColor)) return; // Nothing changed: no PWM writes.
    hasDrawn = true;
    lastLit = lit;
    lastColor = color;
    if (lit) {
        led.setColor(color);
        led.handle(RgbLed::SET_COLOR_COMMAND);
    } else {
        led.handle(RgbLed::TURN_OFF_COMMAND);
    }
}
