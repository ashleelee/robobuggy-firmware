/*
  Read NMEA sentences over I2C using Ublox module SAM-M8Q, NEO-M8P, etc
  By: Nathan Seidle
  SparkFun Electronics
  Date: August 22nd, 2018
  License: MIT. See license file for more information but you can
  basically do whatever you want with this code.

  This example reads the NMEA characters over I2C and pipes them to MicroNMEA
  This example will output your current long/lat and satellites in view

  Feel like supporting open source hardware?
  Buy a board from SparkFun!
  ZED-F9P RTK2: https://www.sparkfun.com/products/15136
  NEO-M8P RTK: https://www.sparkfun.com/products/15005
  SAM-M8Q: https://www.sparkfun.com/products/15106

  For more MicroNMEA info see https://github.com/stevemarple/MicroNMEA

  Hardware Connections:
  Plug a Qwiic cable into the GPS and a BlackBoard
  If you don't have a platform with a Qwiic connection use the SparkFun Qwiic Breadboard Jumper (https://www.sparkfun.com/products/14425)
  Open the serial monitor at 115200 baud to see the output
  Go outside! Wait ~25 seconds and you should see your lat/long
*/

#include "gps.h"
#include "buggyradio.h"
#include "steering.h"
#include "rc.h"
#include "brake.h"

#include <Arduino.h>
#include <Wire.h> //Needed for I2C to GPS
#include <SD.h>

#include <Adafruit_BNO08x.h>

#define RFM69_CS 10
#define RFM69_INT 36
#define RFM69_RST 37

#define RC_SERIAL Serial6
#define BRAKE_RELAY_PIN 26

#define STEERING_PULSE_PIN 27             // pin for stepper pulse
#define STEERING_DIR_PIN 38             // pin for stepper direction
#define STEERING_ALARM_PIN 39
#define LIMIT_SWITCH_RIGHT_PIN 7
#define LIMIT_SWITCH_LEFT_PIN 8

#define BNO_085_INT 20

Adafruit_BNO08x bno08x;
sh2_SensorValue_t sensorValue;

//#include "SparkFun_Ublox_Arduino_Library.h" //http://librarymanager/All#SparkFun_u-blox_GNSS
//SFE_UBLOX_GPS myGPS;

/**  @file

 @brief Universal Transverse Mercator transforms.

 Functions to convert (spherical) latitude and longitude to and
 from (Euclidean) UTM coordinates.

 @author Chuck Gantz- chuck.gantz@globalstar.com
 */

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
// #include "ofMathConstants.h"

//@requires power>=0;
uint64_t positivePow(uint64_t base, uint64_t power)
{

  uint64_t result = 1;
  while (power > 0)
  {
    result *= base;
    power -= 1;
  }
  return result;
}

void setReports(void) {
  Serial.println("Setting desired reports");
  if (!bno08x.enableReport(SH2_ACCELEROMETER)) {
    Serial.println("Could not enable accelerometer");
  }
  if (!bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED)) {
    Serial.println("Could not enable gyroscope");
  }
  if (!bno08x.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED)) {
    Serial.println("Could not enable magnetic field calibrated");
  }
  if (!bno08x.enableReport(SH2_LINEAR_ACCELERATION)) {
    Serial.println("Could not enable linear acceleration");
  }
  if (!bno08x.enableReport(SH2_GRAVITY)) {
    Serial.println("Could not enable gravity vector");
  }
  if (!bno08x.enableReport(SH2_ROTATION_VECTOR)) {
    Serial.println("Could not enable rotation vector");
  }
  if (!bno08x.enableReport(SH2_GEOMAGNETIC_ROTATION_VECTOR)) {
    Serial.println("Could not enable geomagnetic rotation vector");
  }
  if (!bno08x.enableReport(SH2_GAME_ROTATION_VECTOR)) {
    Serial.println("Could not enable game rotation vector");
  }
  if (!bno08x.enableReport(SH2_RAW_ACCELEROMETER)) {
    Serial.println("Could not enable raw accelerometer");
  }
  if (!bno08x.enableReport(SH2_RAW_GYROSCOPE)) {
    Serial.println("Could not enable raw gyroscope");
  }
  if (!bno08x.enableReport(SH2_RAW_MAGNETOMETER)) {
    Serial.println("Could not enable raw magnetometer");
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("SparkFun Ublox Example");

  rc::init(RC_SERIAL);
  brake::init(BRAKE_RELAY_PIN);
  steering::init(STEERING_PULSE_PIN, STEERING_DIR_PIN, STEERING_ALARM_PIN, LIMIT_SWITCH_LEFT_PIN, LIMIT_SWITCH_RIGHT_PIN);

  Wire.begin();

  while (!bno08x.begin_I2C()) {
    Serial.println("BNO085 not detected over I2C. Retrying...");
    delay(1000);
  }

  if (!SD.begin(BUILTIN_SDCARD)) {
    while (1)
      Serial.println("SD card not detected. Freezing");
  }

  gps_init();

  radio_init(RFM69_CS, RFM69_INT, RFM69_RST);

  setReports();

  delay(1000);

  steering::calibrate();
}

bool led_state = false;

uint64_t last_gps_time = 0;
uint64_t last_local_time = millis();

void loop()
{
  int fileNum = 0;
  char fileName[100];
  while (true) {
    snprintf(fileName, 100, "log%d.csv", fileNum);

    if (!SD.exists(fileName)) {
      break;
    }

    ++fileNum;
  }
  File f = SD.open(fileName, FILE_WRITE);
  
  if (!f) {
    while (1)
      Serial.println("File not created. Freezing");
  }

  f.printf("------- BEGIN NEW LOG --------\n");

  unsigned long last_imu_update = millis();

  /*             CSV Format                  */
  /* timestamp, type of data, <rest of data> */

  while (1) {
    /* ================================================ */
    /* Handle RC/autonomous control of steering/braking */
    /* ================================================ */

    rc::update();

    //float steering_command = rc::use_autonomous_steering() ? ros_comms::steering_angle() : rc::steering_angle();
    float steering_command = rc::steering_angle();
    steering::set_goal_angle(steering_command);

    brake::Status brake_command = brake::Status::Stopped;
    if (rc::operator_ready() && !steering::alarm_triggered()) {
      // Only roll if:
      // 1. The person holding the controller is holding down the buttons actively
      // 2. The steering servo is still working
      brake_command = brake::Status::Rolling;
    }

    brake::set(brake_command);

    if (auto gps_coord = gps_update()) {
      Serial.print("x: ");
      Serial.println(gps_coord->x);
      Serial.print("y: ");
      Serial.println(gps_coord->y);
      Serial.print("accuracy: ");
      Serial.println(gps_coord->accuracy);
      Serial.print("time: ");
      Serial.println(gps_coord->gps_time);
      Serial.print("fix type: ");
      Serial.println(gps_coord->fix);
      radio_send_gps(gps_coord->x, gps_coord->y, gps_coord->gps_time, gps_coord->fix);
      f.printf("%lu,GPS,%f,%f,%f,%f\n", millis(), gps_coord->x,gps_coord->y,gps_coord->gps_time,gps_coord->fix);
    }

    if (millis() - last_imu_update > 5) {
      last_imu_update = millis();

      f.printf("%lu,steering,%f\n", millis(), steering::current_angle_degrees());

      if (bno08x.wasReset()) {
        Serial.print("sensor was reset ");
        setReports();
      }

      if (bno08x.getSensorEvent(&sensorValue)) {
        Serial.println("Logging IMU event");

        f.printf("%lu,IMU ", millis());
        switch (sensorValue.sensorId) { 
        case SH2_ACCELEROMETER:
          f.printf(
            "Accelerometer,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.accelerometer.x,
            (double)sensorValue.un.accelerometer.y,
            (double)sensorValue.un.accelerometer.z
          );
          break;
        case SH2_GYROSCOPE_CALIBRATED:
          f.printf(
            "Gyro,%f,%f,%f\n",  //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.gyroscope.x,
            (double)sensorValue.un.gyroscope.y,
            (double)sensorValue.un.gyroscope.z
          );
          break;
        case SH2_MAGNETIC_FIELD_CALIBRATED:
          f.printf(
            "Magnetic Field,%f,%f,%f\n",  //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.magneticField.x,
            (double)sensorValue.un.magneticField.y,
            (double)sensorValue.un.magneticField.z
          );
          break;
        case SH2_LINEAR_ACCELERATION:
          f.printf(
            "Linear Acceleration,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.linearAcceleration.x,
            (double)sensorValue.un.linearAcceleration.y,
            (double)sensorValue.un.linearAcceleration.z
          );
          break;
        case SH2_GRAVITY:
          f.printf(
            "Gravity,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.gravity.x,
            (double)sensorValue.un.gravity.y,
            (double)sensorValue.un.gravity.z
          );
          break;
        case SH2_ROTATION_VECTOR:
          f.printf(
            "Rotation Vector,%f,%f,%f,%f\n", //CSV formatting: [TYPE],[R],[I],[J],[K]
            (double)sensorValue.un.rotationVector.real,
            (double)sensorValue.un.rotationVector.i,
            (double)sensorValue.un.rotationVector.j,
            (double)sensorValue.un.rotationVector.k
          );
          break;
        case SH2_GEOMAGNETIC_ROTATION_VECTOR:
          f.printf(
            "Geo-Magnetic Rotation Vector,%f,%f,%f,%f\n",  //CSV formatting: [TYPE],[R],[I],[J],[K]
            (double)sensorValue.un.geoMagRotationVector.real,
            (double)sensorValue.un.geoMagRotationVector.i,
            (double)sensorValue.un.geoMagRotationVector.j,
            (double)sensorValue.un.geoMagRotationVector.k
          );
          break;
        case SH2_GAME_ROTATION_VECTOR:
          f.printf(
            "Game Rotation Vector,%f,%f,%f,%f\n", //CSV formatting: [TYPE],[R],[I],[J],[K]
            (double)sensorValue.un.gameRotationVector.real,
            (double)sensorValue.un.gameRotationVector.i,
            (double)sensorValue.un.gameRotationVector.j,
            (double)sensorValue.un.gameRotationVector.k
          );
          break;
        case SH2_RAW_ACCELEROMETER:
          f.printf(
            "Raw Accelerometer,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.rawAccelerometer.x,
            (double)sensorValue.un.rawAccelerometer.y,
            (double)sensorValue.un.rawAccelerometer.z
          );
          break;
        case SH2_RAW_GYROSCOPE:
          f.printf(
            "Raw Gyro,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.rawGyroscope.x,
            (double)sensorValue.un.rawGyroscope.y,
            (double)sensorValue.un.rawGyroscope.z
          );
          break;
        case SH2_RAW_MAGNETOMETER:
          f.printf(
            "Raw Magnetic Field,%f,%f,%f\n", //CSV formatting: [TYPE],[X],[Y],[Z]
            (double)sensorValue.un.rawMagnetometer.x,
            (double)sensorValue.un.rawMagnetometer.y,
            (double)sensorValue.un.rawMagnetometer.z
          );
          break;
        default:
          f.printf("Unknown\n");
          break;
        }
      }

      static int flush_cnt = 0;

      if (++flush_cnt >= 100) {
        flush_cnt = 0;
        f.flush();

        digitalToggle(LED_BUILTIN);
      }
    }
  }
}

// This function gets called from the SparkFun Ublox Arduino Library
// As each NMEA character comes in you can specify what to do with it
// Useful for passing to other libraries like tinyGPS, MicroNMEA, or even
// a buffer, radio, etc.

/*
void SFE_UBLOX_GPS::processNMEA(char incoming)
{
  // Take the incoming char from the Ublox I2C port and pass it on to the MicroNMEA lib
  // for sentence cracking
  nmea.process(incoming);
}
*/