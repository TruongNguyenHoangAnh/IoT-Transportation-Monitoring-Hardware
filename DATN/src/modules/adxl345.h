
#pragma once

#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

class ADXLModule {
public:
  ADXLModule();
  bool begin();
  bool read(float &x, float &y, float &z);
  
  // Get raw LSB values
  void getRawLSB(int16_t &x_lsb, int16_t &y_lsb, int16_t &z_lsb);
  
private:
  Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
};

// Helper to start the ADXL telemetry task (implemented in adxl345.cpp)
void startAdxlTelemetry(unsigned long stackSize, UBaseType_t priority);
