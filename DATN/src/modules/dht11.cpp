#include "dht11.h"
#include "vehicle_config.h"

// ===== DISABLED: WiFi + MQTT (not needed for real-time sensor data) =====
// #include <WiFi.h>
// #include <PubSubClient.h>

// const char* WIFI_SSID = "H3-908-5G";
// const char* WIFI_PASS = "66668888";
// const char* MQTT_BROKER = "192.168.1.100";
// =========================================================================

// ===== Định danh thiết bị (đưa vào JSON) =====
// VEHICLE_ID is now dynamic from gVehicleConfig.getDeviceId()
// static const char* DEVICE_ID  = "ESP32-AMMO-001";
// static const char* COMPARTMENT= "MAIN_BAY";

// ===== DISABLED: Threshold alerts + WiFi/MQTT (not needed) =====
// static const float AMMO_TEMP_HIGH_C  = 30.0f;
// static const float AMMO_TEMP_CLEAR_C = 29.0f;
// static const float AMMO_HUM_HIGH_P   = 80.0f;
// static const float AMMO_HUM_CLEAR_P  = 75.0f;
// static const int WARMUP_SAMPLES = 2;
// static const unsigned long ALERT_MIN_INTERVAL_MS = 500UL;
// static const uint8_t TELEMETRY_PRINT_EVERY_N = 1;
// static int telemetryPrintCounter = 0;
// static bool thermalAlertActive  = false;
// static bool humidityAlertActive = false;
// static unsigned long lastAlertMs = 0;
// static int sampleCount = 0;
// static WiFiClient espClient;
// static PubSubClient mqttClient(espClient);
// =========================================================================

// The project defines a global `DHTModule dht` in main.cpp. Use that instance here.
extern DHTModule dht;

// ===== DISABLED: WiFi/MQTT functions =====
// void wifiConnectIfNeeded() { ... }
// void mqttConnectIfNeeded() { ... }
// void publishIfConnected() { ... }
// void printTelemetryHuman() { ... }
// void printTelemetryJSON() { ... }
// void maybePrintAlerts() { ... }
// =========================================================================

// ===== Task đọc DHT11 =====
void TaskDHT11(void *pvParameters) {
  for (;;) {
    // ===== REAL DHT11 CODE - Read from actual DHT11 sensor =====
    float h = dht.readHumidity();
    float t = dht.readTemperature(); // °C
    
    // Check data validity
    if (isnan(h) || isnan(t)) {
      Serial.println("[DHT11-ERROR] Sensor read failed (no valid data)");
      t = -999.0f;
      h = -999.0f;
    }

    // ===== DISABLED: Save to sensorData (not used - TaskLoraSend reads directly from dht) =====
    // if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    //   sensorData.temp = t;
    //   sensorData.humidity = h;
    //   xSemaphoreGive(sensorDataMutex);
    // }

    // Debug output (every 5s)
    if (t != -999.0f && h != -999.0f) {
      // Serial.printf("[DHT11] Temp=%.2f°C, Humidity=%.2f%%\r\n", t, h);
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // Read every 5s
  }
}

// Implement DHTModule methods (constructor/ begin/ read helpers)
DHTModule::DHTModule(uint8_t pin, uint8_t type)
  : dht(pin, type) {}

void DHTModule::begin() { dht.begin(); }

float DHTModule::readTemperature() { return dht.readTemperature(); }

float DHTModule::readHumidity() { return dht.readHumidity(); }

// Helper to create the task from other modules (matching header declaration)
void startDhtTask(unsigned long stackSize, UBaseType_t priority) {
  xTaskCreate(TaskDHT11, "DHT11 Sensor", stackSize, NULL, priority, NULL);
}