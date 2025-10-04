#ifndef X27_STEPPER_H
#define X27_STEPPER_H

#include <Arduino.h>

#define SLOW 400        // uS per step
#define FAST 100        // uS per step
#define ACCELRAMP 100   // steps for changing between SLOW and FAST

class X27Stepper {
private:
    int stepPin;
    int dirPin;
    int currentPos;
    int targetPos;
    unsigned long lastStepTime;
    int stepDelay;
    
public:
    X27Stepper(int pinStep, int pinDir);
    void zero();
    void setPos(int pos);
    void update();
    int getCurrentPos() { return currentPos; }
    int getTargetPos() { return targetPos; }
    bool isMoving() { return currentPos != targetPos; }
};

#endif // X27_STEPPER_H
