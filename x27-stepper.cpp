// A stepper class for the SwitechX27 168
// gauge stepper motor.

// It assumes the AX1201728 driver (aka X12.017)

#include "x27-stepper.h"

X27Stepper::X27Stepper(int pinStep, int pinDir, int resetPin) {
    stepPin = pinStep;
    dirPin = pinDir;
    currentPos = 0;
    targetPos = 0;
    lastStepTime = 0;
    stepDelay = SLOW;
    
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, LOW);
    delayMicroseconds(1000);
    digitalWrite(resetPin, HIGH);
}

// This method moves to zero for startup calibration
void X27Stepper::zero() {
    // Move backwards enough steps to guarantee we hit zero

    // X27.168 has 315° range  with 1/3 deg/step
    // With microstepping and the AX1201728 driver
    // it will be 3 * 4 steps per deg

    digitalWrite(dirPin, LOW);  // Set direction to reverse
    for(int i = 0; i < 315*12; i++) {  
        digitalWrite(stepPin, HIGH);
        delayMicroseconds(1);
        digitalWrite(stepPin, LOW);
        delayMicroseconds(SLOW);
    }
    currentPos = 0;
    targetPos = 0;
}

// Set the desired position 
void X27Stepper::setPos(int pos) {
    targetPos = pos;
}

// Advances towards the setPos - does not block,
// and returns. It will meassure the time
// since called last time and decide if it should
// actually make a step
void X27Stepper::update() {
    unsigned long now = micros();
    
    // Check if enough time has passed since last step
    if (now - lastStepTime < stepDelay) {
        return;
    }
    
    // Check if we need to move
    if (currentPos == targetPos) {
        return;
    }
    
    // Determine direction
    if (targetPos > currentPos) {
        digitalWrite(dirPin, HIGH);
        currentPos++;
    } else {
        digitalWrite(dirPin, LOW);
        currentPos--;
    }
    
    // Calculate acceleration/deceleration
    int stepsToGo = abs(targetPos - currentPos);
    int stepsTaken = abs(currentPos);
    
    if (stepsTaken < ACCELRAMP || stepsToGo < ACCELRAMP) {
        // Use linear interpolation for acceleration/deceleration
        int rampPos = min(stepsTaken, stepsToGo);
        stepDelay = SLOW - ((SLOW - FAST) * rampPos / ACCELRAMP);
    } else {
        stepDelay = FAST;
    }
    
    // Make the step
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(1);  // Short pulse
    digitalWrite(stepPin, LOW);

    lastStepTime = now;
}
