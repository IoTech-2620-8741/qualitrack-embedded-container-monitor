/**
 * @file LockServo.cpp
 * @brief Implements the pulses of the latch servo with the LEDC peripheral of the ESP32.
 */
#include "LockServo.h"

namespace {
    constexpr uint32_t PWM_FREQUENCY_IN_HERTZ = 50;
    constexpr uint8_t PWM_RESOLUTION_BITS = 14;
    constexpr uint32_t PERIOD_IN_MICROSECONDS = 1000000 / PWM_FREQUENCY_IN_HERTZ;
    constexpr uint32_t MINIMUM_PULSE_IN_MICROSECONDS = 500;
    constexpr uint32_t MAXIMUM_PULSE_IN_MICROSECONDS = 2400;
    constexpr int MAXIMUM_ANGLE_IN_DEGREES = 180;
}

const Command LockServo::OPEN_COMMAND = Command(LockServo::OPEN_COMMAND_IDENTIFIER);
const Command LockServo::CLOSE_COMMAND = Command(LockServo::CLOSE_COMMAND_IDENTIFIER);

LockServo::LockServo(const int signalGpioPin, const int closedAngleInDegrees, const int openAngleInDegrees,
                     CommandHandler* parentHandler)
    : Actuator(signalGpioPin, parentHandler),
      closedAngleInDegrees(closedAngleInDegrees),
      openAngleInDegrees(openAngleInDegrees),
      open(false),
      attached(ledcAttach(signalGpioPin, PWM_FREQUENCY_IN_HERTZ, PWM_RESOLUTION_BITS)) {
    writeAngle(closedAngleInDegrees);
}

bool LockServo::isOpen() const {
    return open;
}

bool LockServo::isAttached() const {
    return attached;
}

void LockServo::executeCommand(const Command command) {
    if (command.identifier == OPEN_COMMAND_IDENTIFIER && writeAngle(openAngleInDegrees)) {
        open = true;
    } else if (command.identifier == CLOSE_COMMAND_IDENTIFIER && writeAngle(closedAngleInDegrees)) {
        open = false;
    }
}

bool LockServo::writeAngle(const int angleInDegrees) {
    if (!attached) return false;
    const int angle = constrain(angleInDegrees, 0, MAXIMUM_ANGLE_IN_DEGREES);
    const uint32_t pulse = MINIMUM_PULSE_IN_MICROSECONDS
        + (MAXIMUM_PULSE_IN_MICROSECONDS - MINIMUM_PULSE_IN_MICROSECONDS) * angle / MAXIMUM_ANGLE_IN_DEGREES;
    const uint32_t maximumDuty = (1UL << PWM_RESOLUTION_BITS) - 1;
    return ledcWrite(pin, pulse * maximumDuty / PERIOD_IN_MICROSECONDS);
}
