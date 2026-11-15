/**
 * @file LockServo.h
 * @brief SG90 servo of the latch of the container (MS11).
 *
 * The `ServoMotor` of the framework v1.0.0 turns the angle into a share of the whole 20 ms period
 * (90° gives a 10 ms pulse), so a hobby servo does not move. This actuator extends the `Actuator`
 * extension point of the framework and sends the pulses a servo expects: 0.5 ms at 0° to 2.4 ms at
 * 180°, 50 times a second.
 */
#ifndef LOCK_SERVO_H
#define LOCK_SERVO_H

#include <ModestIoT.h>

/**
 * @brief Latch of the container with an open and a closed position.
 */
class LockServo : public Actuator {
public:
    static constexpr int OPEN_COMMAND_IDENTIFIER = 600;
    static constexpr int CLOSE_COMMAND_IDENTIFIER = 601;
    /// Moves the latch to the open angle.
    static const Command OPEN_COMMAND;
    /// Moves the latch to the closed angle.
    static const Command CLOSE_COMMAND;

    /**
     * @brief Attaches the servo and moves it to the closed angle.
     * @param signalGpioPin GPIO of the signal of the servo.
     * @param closedAngleInDegrees Angle of the closed latch.
     * @param openAngleInDegrees Angle of the open latch.
     * @param parentHandler Receives the commands after the servo runs them.
     */
    LockServo(int signalGpioPin, int closedAngleInDegrees, int openAngleInDegrees,
              CommandHandler* parentHandler = nullptr);

    /** @return True when the last command left the latch open. */
    [[nodiscard]] bool isOpen() const;

    /** @return True when the PWM of the servo could be attached. */
    [[nodiscard]] bool isAttached() const;

protected:
    void executeCommand(Command command) override;

private:
    int closedAngleInDegrees;
    int openAngleInDegrees;
    bool open;
    bool attached;

    /** @return True when the pulse of the angle was written. */
    bool writeAngle(int angleInDegrees);
};

#endif // LOCK_SERVO_H
