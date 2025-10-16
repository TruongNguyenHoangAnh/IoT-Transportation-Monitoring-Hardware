
#pragma once

#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>

class ADXLModule {
public:
  ADXLModule();
  bool begin();
  bool read(float &x, float &y, float &z);
private:
  Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
};

// Helper to start the ADXL telemetry task (implemented in adxl345.cpp)
void startAdxlTelemetry(unsigned long stackSize, UBaseType_t priority);
