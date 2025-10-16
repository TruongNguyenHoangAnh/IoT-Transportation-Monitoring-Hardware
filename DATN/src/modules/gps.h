#pragma once

#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

class GPSModule {
public:
  GPSModule(HardwareSerial &serial, unsigned long baud = 9600);
  void begin();
  // begin with explicit RX/TX pins and optional baud
  void beginPins(int rxPin, int txPin, unsigned long baud = 0);
  void loop();
  bool hasFix();
  double latitude();
  double longitude();
  String timestamp();
  // Full example helpers (prints detailed GPS status to Serial)
  void printFullExampleHeader();
  void printFullExampleRow();
private:
  TinyGPSPlus gps;
  HardwareSerial &ser;
  unsigned long baudRate;
  // helper functions used by the full example
  void smartDelay(unsigned long ms);
  void printFloat(float val, bool valid, int len, int prec);
  void printInt(unsigned long val, bool valid, int len);
  void printDateTime(TinyGPSDate &d, TinyGPSTime &t);
  void printStr(const char *str, int len);
public:
  // Create a FreeRTOS task that runs the full-example printing routine
  friend void startGpsFullTask(GPSModule &mod, unsigned long stackSize, UBaseType_t priority);
};
