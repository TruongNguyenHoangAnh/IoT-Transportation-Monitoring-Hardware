
#pragma once

#include <Arduino.h>
#include <DHT.h>

class DHTModule {
public:
  DHTModule(uint8_t pin, uint8_t type = DHT11);
  void begin();
  float readTemperature();
  float readHumidity();
private:
  DHT dht;
};

// Helper to start the DHT FreeRTOS task from other files
void startDhtTask(unsigned long stackSize, UBaseType_t priority);
