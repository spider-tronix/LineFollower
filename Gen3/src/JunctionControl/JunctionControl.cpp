#include "JunctionControl.h"

#include <Arduino.h>

#include "../TesterInterface/TesterInterface.h"
#include "../SensorInterface/SensorInterface.h"
#include "../PushButtonInterface/PushButtonInterface.h"
#include "../MotorDriverInterface/MotorDriverInterface.h"
#include "../LEDInterface/LEDInterface.h"
#include "../AsyncUltrasonic/AsyncUltrasonic.h"
#include "../configure.h"
#include "../Timer/Timer.h"

#ifndef ALGORITHM_LOGIC
#define ALGORITHM_LOGIC LEFT_LOGIC
#endif

#ifndef MOTOR_OVERSHOOT_SPEED
#define MOTOR_OVERSHOOT_SPEED 100
#endif

#ifndef MOTOR_TURN_SPEED
#define MOTOR_TURN_SPEED 0
#endif

#ifndef MOTOR_EXCESS_TURN_SPEED
#define MOTOR_EXCESS_TURN_SPEED 150
#endif

#ifndef MOTOR_JUNCTION_DELAY
#define MOTOR_JUNCTION_DELAY 500
#endif

#ifndef MIN_OVERSHOOT_TIME
#define MIN_OVERSHOOT_TIME 200
#endif

JunctionControl::JunctionControl(Sensors &_sensors, Motor &_motors, MotorPIDControl& _pid) : sensors(_sensors), motors(_motors), pid(_pid)
{
    backSensorState = 0;
    CFState = false;
    blockDetectFlag = false;
    mode = DRY_RUN;
    paused = 0;
    currentJunction = 0;
}

void JunctionControl::pause(uint32_t m) {
    paused = m + millis();
}

void JunctionControl::setAlgorithm(Algorithm algo) {
    algorithm  = algo;
}

void JunctionControl::addPathChoice(const char* choice) {
    strcat(path, choice);
}

void JunctionControl::updateState()
{
    sensors.readSensors();
    sensors.convertAnalogToDigital();
    uint16_t sensorValues = sensors.digitalValues;

    VD(Debug::print("\nSensor: ");
    Debug::print((uint32_t)(sensorValues), 2);
    Debug::print("\tJunction: ");)

    backSensorState = sensorValues & (0b1111);
    backSensorState |= (sensorValues & (0b111100000)) >> 1;
    backSensorStateInverted = (backSensorState * 0x0202020202ULL & 0x010884422010ULL) % 1023; // Contains backSensorState's mirror inversion
    CFState = sensorValues & 0b000010000;

    blockDetectFlag =  AsyncUltrasonic::detectBlock();
}

void JunctionControl::detect()
{
    updateState();
    static uint16_t prevSensorValues = 0;
                      
    if (millis() > paused)
    if (blockDetectFlag == 1)
    {
        control(BL);
        blockDetectFlag = 0;
    }
    else if ((CFState == 0) && (backSensorState & 0b11) == 0b11 && (backSensorStateInverted & 0b11) == 0b11)
    {
        control(T);
    }
    // // Straight Right detection
    else if ((sensors.digitalValues & 0b000010011) == 0b000010011 
                && (prevSensorValues & 0b000010011) == 0b000010011)
    {
        control(RS);
    }
    // // Straight Left detection
    else if ((sensors.digitalValues & 0b110010000) == 0b110010000 
                && (prevSensorValues & 0b110010000) == 0b110010000)
    {
        control(LS);
    }
    // else if (sensors.digitalValues == 0b111001100 || sensors.digitalValues == 0b001100111) { //*
    //     control(T);
    // } 
    else if ((CFState == 0) && ((backSensorState & 0b11100010) == 0b11100010 )) {
        
        control(T);
    }
    else if ((CFState == 0) && ((backSensorStateInverted & 0b11100010) == 0b11100010 )) {
        control(T);
    }
    else if (sensors.digitalValues == 0b110000110 || sensors.digitalValues == 0b011000011) {
        control(T);
    }
    else if (sensors.digitalValues == 0b110001100 || sensors.digitalValues == 0b001100011) {
        control(T);
    }
    else if (sensors.digitalValues == 0b110001110 || sensors.digitalValues == 0b011100011) {
        control(T);
    }
    else if (sensors.digitalValues == 0b100000110 || sensors.digitalValues == 0b011000001) {
        control(T);
    }
    else if (sensors.digitalValues == 0b111001100 || sensors.digitalValues == 0b001100111) {
        control(T);
    }
    else if((CFState==1) && (backSensorStateInverted == 255)) {
        control(X);
    }
    // //90 degree right
    else if ((CFState == 0) && (backSensorState == 3) )
    {
        control(R);
    }
    // //90 degrees left
    else if ((CFState == 0) && (backSensorStateInverted == 3) ) 
    {
        control(L);     
    }
    else if (sensors.digitalValues == 0b111000000 || sensors.digitalValues == 0b010000000) {
        control(L);
    }
    else if (sensors.digitalValues == 0b000000111 || sensors.digitalValues == 0b000000010) {
        control(R);
    }
    else if (backSensorState == 0b11111000 && CFState == 0) control(L);
    else if (backSensorStateInverted == 0b11111000 && CFState == 0) control(R);
    else if (CFState == 0 && backSensorState == 0) {
        control(END);
    }

    prevSensorValues = sensors.digitalValues;
}

void JunctionControl::control(Junction j)
{
    Debug::print(junctionNames[j]);
//*
    if (mode == DRY_RUN) {
        if (algorithm == LEFT_LOGIC)
        {
            switch (j)
            {
            case BL:
                turnAroundForBlock();
                addPathChoice("b");
                break;
            case L:
            case LS:
            case T:
                turnLeft();
                if (j != L) addPathChoice("l");
                break;

            case R:
                turnRight();
                break;

            case X:
                if (!turnLeft()) {
                    motors.stopMotors();
                    Debug::println(path);
                    Solver::solve(path, algorithm);
                    Debug::println(path);

                    Timer t;
                    int8_t id = t.oscillate(LED_PIN_RED, 500, LOW);
                    while (1) {
                        t.update();

                        if (PushButtonInterface::read(0)) {
                            break;
                        }
                    }
                    
                    t.stop(id);
                    LED::write(0, HIGH);
                    LED::write(1, HIGH);
                    mode = ACTUAL_RUN;
                    break;
                }
                addPathChoice("l");
                break;

            case RS:
                if  (!(sensors.readRearSensors() & 0b000000001))
                    sensors.addRightOvershoot();
                pause();
                addPathChoice("s");
                break;

            case END:
                turnAroundForEnd();
                addPathChoice("b");
                break;
            }
            pid.setBaseSpeed(POST_JUNCTION_BASE_SPEED);
        }
        else {
            switch (j) {
            case L:
                turnLeft();
                break;
            case RS:
            case T:
            case R:
                turnRight();
                if (j != R) addPathChoice("r");
                break;
            case X:
                if (!turnRight()) {
                    motors.stopMotors();
                    Debug::println(path);
                    Solver::solve(path, algorithm);
                    Debug::println(path);
                    
                    Timer t;
                    int8_t id = t.oscillate(LED_PIN_RED, 500, LOW);
                    while (1) {
                        t.update();

                        if (PushButtonInterface::read(0)) {
                            break;
                        }
                    }
                    
                    t.stop(id);
                    LED::write(0, HIGH);
                    LED::write(1, HIGH);
                    mode = ACTUAL_RUN;
                    break;
                }
                addPathChoice("r");
                break;
            case END:
                turnAroundForEnd();
                addPathChoice("b");
                break;
            case BL:
                turnAroundForBlock();
                addPathChoice("b");
                break;
            case LS:
                if  (!(sensors.readRearSensors() & 0b000000010))
                        sensors.addLeftOvershoot();
                pause();
                addPathChoice("s");
                break;
            }
            pid.setBaseSpeed(POST_JUNCTION_BASE_SPEED);
           
        }
    }
    else {
        switch(j) {
            case R:
                turnRight();
                break;
            case L:
                turnLeft();
                break;
            case X:
            case T:
                if (path[currentJunction] == 'r') {
                    turnRight();
                }
                else if (path[currentJunction] == 'l'){
                    turnLeft();
                }
                else if (path[currentJunction] = '\0') {
                    motors.stopMotors();
                    Debug::println("END");
                    while (1);
                }
                else {
                    if  (!(sensors.readRearSensors() & 0b000000010))
                        sensors.addLeftOvershoot();
                    pause();
                }

                ++currentJunction;
                break;
            case LS:
                if (path[currentJunction] == 'l') {
                    turnLeft();
                }
                else {
                    if  (!(sensors.readRearSensors() & 0b000000010))
                        sensors.addLeftOvershoot();
                    pause();
                }
                ++currentJunction;
                break;

            case RS:
                if (path[currentJunction] == 'r') {
                    turnRight();
                }
                else {
                    if  (!(sensors.readRearSensors() & 0b000000001))
                        sensors.addRightOvershoot();
                    pause();
                }
                ++currentJunction;
                break;
        }
        pid.setBaseSpeed(POST_JUNCTION_BASE_SPEED);
    }
    /**/
}

void JunctionControl::turnAroundForBlock() {
    motors.stopMotors();

    motors.setLeftDirection(Motor::BACK);
    motors.setRightDirection(Motor::FRONT);
    motors.setLeftSpeed(MOTOR_EXCESS_TURN_SPEED / 1.3);
    motors.setRightSpeed(MOTOR_EXCESS_TURN_SPEED / 1.3);

    LED::write(0, HIGH);
    delay(750);
    while (sensors.rearCenterStatus());
    LED::write(1, HIGH);
    while (!sensors.rearCenterStatus());
    LED::write(0, LOW);

    motors.setRightDirection(Motor::BACK);
    motors.setLeftDirection(Motor::BACK);
    motors.setLeftSpeed(MOTOR_OVERSHOOT_SPEED);
    motors.setRightSpeed(MOTOR_OVERSHOOT_SPEED);

    while(CFState != 1) {
        updateState();
    }

    LED::write(1, LOW);

    AsyncUltrasonic::distance = 0;
    delay(BLOCK_REVERSE_DELAY);
    pid.increasePID();
}

void JunctionControl::turnAroundForEnd() {
    motors.setLeftSpeed(MOTOR_OVERSHOOT_SPEED);
    motors.setRightSpeed(MOTOR_OVERSHOOT_SPEED);

    motors.setLeftDirection(Motor::FRONT);
    motors.setRightDirection(Motor::FRONT);
    delay(500);

    while (sensors.rearCenterStatus());

    motors.setLeftSpeed(MOTOR_EXCESS_TURN_SPEED/1.3);
    motors.setRightSpeed(MOTOR_EXCESS_TURN_SPEED/1.3);

    motors.setLeftDirection(Motor::BACK);
    motors.setRightDirection(Motor::FRONT);

    delay(750);

    do  {
        updateState();
        VD(Debug::print("l");)
    } while (!(CFState == 1 && (backSensorState & 0b00010000) == 0b00010000));

    pid.increasePID();
}

bool JunctionControl::turnLeft() {
    //overshoot
    if  (!(sensors.readRearSensors() & 0b000000010))
        sensors.addLeftOvershoot();

    uint32_t time = millis();

    motors.setLeftSpeed(MOTOR_OVERSHOOT_SPEED);
    motors.setRightSpeed(MOTOR_OVERSHOOT_SPEED);

    motors.setLeftDirection(Motor::FRONT);
    motors.setRightDirection(Motor::FRONT);
    
    sensors.waitForOvershoot();
    while (millis() - time < MIN_OVERSHOOT_TIME);
    // motors.stopMotors();
    // PushButtonInterface::waitForButton(0);

    //finish
    updateState();
    if (sensors.digitalValues == 0b111111111) return false;

    //turn
    motors.setLeftDirection(Motor::BACK);
    motors.setRightDirection(Motor::FRONT);
    motors.setLeftSpeed(MOTOR_EXCESS_TURN_SPEED);
    motors.setRightSpeed(MOTOR_TURN_SPEED);

    time = millis();
    do
    {
        updateState();
        VD(Debug::print("l");)
    } while ((CFState == 1));
    while (millis() - time < MIN_TURN_TIME);

    do
    {
        updateState();
        VD(Debug::print("l");)
        // Debug::print(backSensorState & 0b10000000, 2);
    } while (!((CFState == 1) && ((backSensorState & 0b10011000) == 0b00011000)));// && (backSensorState & 0b00000011) != 0);

    if  (!(sensors.readRearSensors() & 0b000000010))
        sensors.addLeftOvershoot();
    pid.increasePID();
    return true;
}

bool JunctionControl::turnRight() {
    //overshoot
    if  (!(sensors.readRearSensors() & 0b000000001))
        sensors.addRightOvershoot();

    uint32_t time = millis();

    motors.setLeftSpeed(MOTOR_OVERSHOOT_SPEED);
    motors.setRightSpeed(MOTOR_OVERSHOOT_SPEED);

    motors.setLeftDirection(Motor::FRONT);
    motors.setRightDirection(Motor::FRONT);
    
    sensors.waitForOvershoot();
    while (millis() - time < MIN_OVERSHOOT_TIME);

    // motors.stopMotors();
    // PushButtonInterface::waitForButton(0);
    //finish
    updateState();
    if (sensors.digitalValues == 0b111111111) return false;

    //turning
    motors.setLeftDirection(Motor::FRONT);
    motors.setRightDirection(Motor::BACK);
    motors.setRightSpeed(MOTOR_EXCESS_TURN_SPEED);
    motors.setLeftSpeed(MOTOR_TURN_SPEED);

    time = millis();
    do
    {
        updateState();
        VD(Debug::print("r");)
    } while ((CFState == 1));
    while (millis() - time < MIN_TURN_TIME);

    do
    {
        updateState();  
        VD(Debug::print("r");)
    } while (!((CFState == 1) && ((backSensorStateInverted & 0b10011000) == 0b00011000)));// && (backSensorState & 0b00000011) != 0);
    
    
    
    if  (!(sensors.readRearSensors() & 0b000000001))
        sensors.addRightOvershoot();
    pid.increasePID();
    return true;
}