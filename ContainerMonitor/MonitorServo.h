#ifndef MONITOR_SERVO_H
#define MONITOR_SERVO_H

#include "Actuator.h"
#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @brief Versión local de ServoMotor para compatibilidad con Wokwi.
 * Mantiene la misma interfaz pública que el framework original.
 */
class MonitorServo : public Actuator {
private:
    int currentActiveAngleInDegrees;
    int bufferedTargetAngleInDegrees;

    // Instancia estática del servo de ESP32Servo
    static Servo& hardwareServo() {
        static Servo servo;
        return servo;
    }
    static bool& isAttached() {
        static bool attached = false;
        return attached;
    }

    static constexpr int MINIMUM_PHYSICAL_ANGLE_IN_DEGREES = 0;
    static constexpr int MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES = 180;

    void writeAngle(int angleInDegrees) const {
        if (isAttached()) {
            int clamped = angleInDegrees;
            if (clamped < MINIMUM_PHYSICAL_ANGLE_IN_DEGREES) clamped = MINIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            if (clamped > MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES) clamped = MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            hardwareServo().write(clamped);
        }
    }

protected:
    void executeCommand(Command command) override {
        if (command == LOCK_TO_MINIMUM_LIMIT_COMMAND) {
            currentActiveAngleInDegrees = MINIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            writeAngle(currentActiveAngleInDegrees);
        }
        else if (command == LOCK_TO_MAXIMUM_LIMIT_COMMAND) {
            currentActiveAngleInDegrees = MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            writeAngle(currentActiveAngleInDegrees);
        }
        else if (command == DRIVE_TO_BUFFERED_ANGLE_COMMAND) {
            currentActiveAngleInDegrees = bufferedTargetAngleInDegrees;
            writeAngle(currentActiveAngleInDegrees);
        }
        else if (command.identifier == DRIVE_TO_ANGLE_COMMAND_IDENTIFIER) {
            currentActiveAngleInDegrees = command.payload;
            if (currentActiveAngleInDegrees < MINIMUM_PHYSICAL_ANGLE_IN_DEGREES) currentActiveAngleInDegrees = MINIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            if (currentActiveAngleInDegrees > MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES) currentActiveAngleInDegrees = MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES;
            writeAngle(currentActiveAngleInDegrees);
        }
    }

public:
    static constexpr int LOCK_TO_MINIMUM_LIMIT_COMMAND_IDENTIFIER = 150;
    static constexpr int LOCK_TO_MAXIMUM_LIMIT_COMMAND_IDENTIFIER = 151;
    static constexpr int DRIVE_TO_BUFFERED_ANGLE_COMMAND_IDENTIFIER = 152;
    static constexpr int DRIVE_TO_ANGLE_COMMAND_IDENTIFIER = 153;

    static constexpr auto LOCK_TO_MINIMUM_LIMIT_COMMAND = Command(LOCK_TO_MINIMUM_LIMIT_COMMAND_IDENTIFIER);
    static constexpr auto LOCK_TO_MAXIMUM_LIMIT_COMMAND = Command(LOCK_TO_MAXIMUM_LIMIT_COMMAND_IDENTIFIER);
    static constexpr auto DRIVE_TO_BUFFERED_ANGLE_COMMAND = Command(DRIVE_TO_BUFFERED_ANGLE_COMMAND_IDENTIFIER);

    explicit MonitorServo(int signalGpioPin, int initialAngleInDegrees = MINIMUM_PHYSICAL_ANGLE_IN_DEGREES, CommandHandler* parentHandler = nullptr)
        : Actuator(signalGpioPin, parentHandler),
          currentActiveAngleInDegrees(initialAngleInDegrees),
          bufferedTargetAngleInDegrees(initialAngleInDegrees)
    {
        hardwareServo().attach(pin);
        isAttached() = true;
        writeAngle(currentActiveAngleInDegrees);
    }

    ~MonitorServo() override {
        if (isAttached()) {
            hardwareServo().detach();
            isAttached() = false;
        }
    }

    static int validateOrAdjustPhysicalAngle(int physicalAngleInDegrees) {
        if (physicalAngleInDegrees < MINIMUM_PHYSICAL_ANGLE_IN_DEGREES) return MINIMUM_PHYSICAL_ANGLE_IN_DEGREES;
        if (physicalAngleInDegrees > MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES) return MAXIMUM_PHYSICAL_ANGLE_IN_DEGREES;
        return physicalAngleInDegrees;
    }

    void setTargetAngleBufferInDegrees(int targetAngleInDegrees) {
        bufferedTargetAngleInDegrees = validateOrAdjustPhysicalAngle(targetAngleInDegrees);
    }

    int getCurrentActiveAngleInDegrees() const { return currentActiveAngleInDegrees; }
};

#endif // MONITOR_SERVO_H