#include "dht11.h"

DHTModule::DHTModule(uint8_t pin, uint8_t type)
  : dht(pin, type) {}

void DHTModule::begin() { dht.begin(); }

float DHTModule::readTemperature() { return dht.readTemperature(); }

float DHTModule::readHumidity() { return dht.readHumidity(); }

// ---- Optional FreeRTOS task to read DHT11 ----
// This task expects there to be a global `DHTModule dht` defined elsewhere (for example in src/main.cpp).
extern DHTModule dht; // main.cpp defines this instance

// Task that reads DHT periodically and prints to Serial
void TaskDHT11(void *pvParameters) {
  // Ensure sensor initialized by caller (dht.begin())
  for (;;) {
    float h = dht.readHumidity();
    float t = dht.readTemperature(); // °C

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      Serial.print("Humidity: ");
      Serial.print(h);
      Serial.print(" %\tTemperature: ");
      Serial.print(t);
      Serial.println(" °C");
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // 5s
  }
}

// Helper to create the task from other modules
void startDhtTask(unsigned long stackSize, UBaseType_t priority) {
  xTaskCreate(TaskDHT11, "DHT11 Sensor", stackSize, NULL, priority, NULL);
}
