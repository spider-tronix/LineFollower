#define LF_WHITELINE_LOGIC

#include "QTRSensorsAnalog.h"
#include "pid_control.h"
#include "motor_control.h"
#include <SoftwareSerial.h>

#define redLED LED_BUILTIN
#define yellowLED 2
#define button 12

#define PID_IDEAL 3500
#define baseSpeed 255
#define SENSOR_PINS \
    (const uint8_t[]) { A0, A1, A2, A3, A4, A5, A6, A7 }
#define NUM_SENSORS 8

SoftwareSerial bluetooth(12, 11);
#define MOTOR_RIGHT_PINS \
    (const uint8_t[]) { 7, 8, 10 }
#define MOTOR_LEFT_PINS \
    (const uint8_t[]) { 5, 6, 9 }
#define MOTOR_STANDBY_PIN \
    (const uint8_t)4

QTRSensorsAnalog qtr(SENSOR_PINS, NUM_SENSORS);
uint16_t sensorValues[NUM_SENSORS], thresholdValues[NUM_SENSORS], thresholdValues2[NUM_SENSORS];
uint16_t line = 0;
uint8_t sensors;
PIDControl pid(PID_IDEAL);
Motor motor(MOTOR_LEFT_PINS, MOTOR_RIGHT_PINS, MOTOR_STANDBY_PIN);

enum Junction
{
    T,
    R,
    L,
    X,
    RS,
    LS,
    FINISH,
    END
};
char juncs[] = "TRLXtrl";

void PID_tune() // Auto tune implemented using twiddle algorithm
{
    //bluetooth.println("PID tuning...");
    float best_err = abs(pid.getError(qtr.readLine(sensorValues)));
    float err;

    double sum = (pid.dp[0] + pid.dp[1] + pid.dp[2]);

    while (sum > 0.001)
    {
        for (int i = 0; i < 2; i++)
        {
            pid.parameters[i] += pid.dp[i];
            err = follow();
            if (err < best_err)
            {
                best_err = err;
                pid.dp[i] *= 1.1;
            }
            else
            {
                pid.parameters[i] -= 2 * pid.dp[i];
                err = follow();

                if (err < best_err)
                {
                    best_err = err;
                    pid.dp[i] *= 1.1;
                }
                else
                {
                    pid.parameters[i] += pid.dp[i];
                    pid.dp[i] *= 0.9;
                }
            }
        }
        sum = (pid.dp[0] + pid.dp[1] + pid.dp[2]);
    }

    //bluetooth.println("Done PID tuning");
    //bluetooth.println("kp: ");
    //bluetooth.println(*pid.kp);
    //bluetooth.println("kd: ");
    //bluetooth.println(*pid.kd);
    //bluetooth.println("ki: ");
    //bluetooth.println(*pid.ki);
}

void setup()
{
    bluetooth.begin(9600);

    pinMode(button,INPUT);
    pinMode(yellowLED, OUTPUT);
    pinMode(redLED,OUTPUT);
    //callibration
    digitalWrite(2, HIGH);
    //bluetooth.println("Calibrating");
    for (int i = 0; i < 400; i++)
    {
        qtr.calibrate();
    }
    qtr.generateThreshold(thresholdValues, thresholdValues2);
    //bluetooth.println("Done calib");
    //bluetooth.println("thresholdValues");
    for (int i = 0; i < NUM_SENSORS; i++)
    {
        //bluetooth.print(thresholdValues[i]);
        //bluetooth.print(" ");
    }
    //bluetooth.println();
    for (int i = 0; i < NUM_SENSORS; i++)
    {
        //bluetooth.print(thresholdValues2[i]);
        //bluetooth.print(" ");
    }
    //bluetooth.println();
    //PID_tune();
    digitalWrite(2, LOW);
}
unsigned int follow()
{

    int16_t correction = pid.control((int)line);

    //bluetooth.println(correction);
    int16_t newSpeed = baseSpeed;
    newSpeed -= abs(correction);
    if (newSpeed < 0)
        newSpeed = 0;

    if (correction > 0)
    {
        motor.setLeftSpeed(baseSpeed);
        motor.setRightSpeed(newSpeed);
    }
    else
    {
        motor.setLeftSpeed(newSpeed);
        motor.setRightSpeed(baseSpeed);
    }

    motor.setLeftDirection(Motor::Front);
    motor.setRightDirection(Motor::Front);

    return abs(pid.getError(qtr.readLine(sensorValues)));
}

void loop()
{
    line = qtr.readLine(sensorValues);
    sensors = sensorValuesInBinary();
    follow();
    junctionDetect();
}

uint8_t sensorValuesInBinary()
{
    uint8_t sensors = 0;
#ifdef LF_WHITELINE_LOGIC
    for (int i = 0; i < 8; i++)
    {
        if (sensorValues[i] < thresholdValues2[i])
        {
            sensors |= (1 << (7 - i));
        }
    }
#else
    for (int i = 0; i < 8; i++)
    {
        if (sensorValues[i] > thresholdValues2[i])
        {
            sensors |= (1 << (7 - i));
        }
    }
#endif
    return sensors;
}
void stopCar(int time)
{
    motor.stopMotors();
    delay(time);
}
void junctionDetect()
{
    uint8_t j;
    bluetooth.println(sensors,BIN);

    if ((sensors & 0b10000001) == 0b10000001)
    {
        j = T;
    }
    else if ((sensors & 0b11111001) == 0b11111000)
    {
        j = L;
    }
    else if ((sensors & 0b11011111) == 0b00011111)
    {
        j = R;
    }
    else if( sensors == 0b00000000)
    {
        j=END;
    }
    else
    {
        return;
    }
    //stopCar(2000);

    motor.setLeftDirection(Motor::Front);
    motor.setRightDirection(Motor::Front);
    motor.setLeftSpeed(100);
    motor.setRightSpeed(100);
    delay(250);

    stopCar(500);

    qtr.readLine(sensorValues);
    sensors = sensorValuesInBinary();

    if ( ((sensors & 0b00111100) == 0b00111100) || ((sensors & 0b00111000) == 0b00111000) || ((sensors & 0b00011100) == 0b00011100) )
    {
        j += 3;
    }
    if ((sensors & 0b10000001) == 0b10000001)
    {
        j = FINISH;
    }

    junctionControl(j);
}

void junctionControl(Junction J)
{
    bluetooth.println(juncs[J]);
    uint8_t sensors;
    switch (J)
    {
    case L:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while (!(sensors == 0b00111100));
        delay(20);
        break;
    case R:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setRightDirection(Motor::Back);
            motor.setLeftDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);
            //bluetooth.println("right");
        } while (!(sensors == 0b00111100));
        //delay(20);
        break;
    case T:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while (!(sensors == 0b00111100));
        delay(20);
        break;
    case RS:  // Simply move forward

        break;
    case LS:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while ((sensors == 0b00111100));

        delay(50);

        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while (!(sensors == 0b00111100));
        break;
    
    case X:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while ((sensors == 0b00111100));

        delay(50);

        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            //bluetooth.println("left");
        } while (!(sensors == 0b00111100));
        break;
    case END:
        do
        {
            qtr.readLine(sensorValues);
            sensors = sensorValuesInBinary();

            motor.setLeftDirection(Motor::Back);
            motor.setRightDirection(Motor::Front);
            motor.setLeftSpeed(100);
            motor.setRightSpeed(100);

            bluetooth.println("U-turn");
        } while (!(sensors == 0b00111100));
        break;

    case FINISH:
        bluetooth.println("Dry run complete");
        stopCar(10);
        while(1)
        {
            digitalWrite(redLED,HIGH);
            delay(750);
            digitalWrite(redLED,LOW);
            delay(750);
            if(digitalRead(button) == LOW)
            {
                return;
            }
        }
        break;
    default:
        break;
    }
}