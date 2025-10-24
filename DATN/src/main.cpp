#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>

#include "modules/gps.h"
#include "modules/dht11.h"
#include "modules/adxl345.h"

// Configuration - change pins/baud here if needed
#define DHTPIN   14
#define DHTTYPE  DHT11
#define LED_PIN  2

// GPS uses Serial1 by default (UART1). Change pins below if your wiring uses different UART pins.
// Example: GPS on RX=16, TX=17 -> call gps.beginPins(16, 17, 4800);
GPSNeo6M gps(16, 17, 9600);
// DHT module instance is a lightweight wrapper around the DHT library
DHTModule dht(DHTPIN, DHTTYPE);
// ADXL345 module (Adafruit Unified wrapper)
ADXLModule adxl;
// ---- LED heartbeat task ----
void TaskLED(void *pvParameters) {
  pinMode(LED_PIN, OUTPUT);
  bool on = false;
  for (;;) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    on = !on;
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
// ---- Main setup & task creation ----
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("DATN: initializing modules...");

 // Initialize Wire for I2C devices (ADXL345)
  Wire.begin();

  // Ensure sensors are initialized before tasks start where necessary
  dht.begin();

  if (!adxl.begin()) {
   Serial.println("Warning: ADXL345 not found (check wiring)");
  }


  // Start LED heartbeat
  xTaskCreate(TaskLED, "LED", 1024, NULL, 1, NULL);


  // Start DHT reader task (uses helper in dht11.cpp)
  startDhtTask(2048, 1);


  // Start ADXL telemetry task (raw I2C CSV telem)
  startAdxlTelemetry(4096, 1);


  // Start GPS: use RX=16, TX=17 on Serial1 for this wiring and 9600 baud
 gps.begin();
}


void loop() {
  // Nothing needed here — FreeRTOS tasks are running. Keep the loop alive with a delay.
 vTaskDelay(pdMS_TO_TICKS(1000));
 gps.read();
 gps.printLocation();
 delay(5000);
}