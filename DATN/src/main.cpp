#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <esp_system.h>   // For chip ID functions

#include "modules/gps.h"
#include "modules/dht11.h"
#include "modules/adxl345.h"
#include "modules/ldr.h"
#include "modules/vehicle_config.h"
#include "modules/local_memory.h"   // SD logging
#include "modules/sensor_Data.h"    // Shared sensor data struct + mutex
// #include "modules/lora.h"      // <-- bỏ wrapper LoRa AT (không cần)

//
// ===== Pins / Config =====
#define DHTPIN    14
#define DHTTYPE   DHT11
#define LED_PIN   2
#define LDR_PIN   35      // ADC pin for light sensor (tamper detection)

// === VEHICLE CONFIGURATION ===
// Change this for each vehicle in the convoy:
// Set via compile-time macro: -DVEHICLE_DEVICE_ID=\"VX-01\" (in platformio.ini)
// Or via runtime EEPROM after first boot
#ifndef VEHICLE_DEVICE_ID
  #define VEHICLE_DEVICE_ID "VX"
#endif

// GPS: UART2 RX=16, TX=17 @ 9600
GPSNeo6M  gps(16, 17, 9600);
DHTModule dht(DHTPIN, DHTTYPE);
ADXLModule adxl; // ADXL345
LDRModule ldr(LDR_PIN); // Light sensor for tamper detection

// --- LoRa UART (RA-08H TX connected here) on UART1 ---
#define LORA_RX   25     // ESP32 RX1  <= TX of RA-08H
#define LORA_TX   26     // ESP32 TX1  => RX of RA-08H
#define LORA_BAUD 115200 // match pingpong_tx UART baud

HardwareSerial LORA_SER(1); // UART1

volatile uint32_t g_send_interval_ms = 2000; // mặc định 2s (send interval)
volatile bool g_tamper_alert = false;         // Tamper alert flag
static uint32_t _seq = 0;

// ---- Helper: Detect node_id from LoRa TX OR generate from Chip ID ----
// Try to listen for LoRa TX startup message "[LORA-NODE-ID] XYZ"
// If timeout, fallback to generating unique node_id from ESP32 Chip ID
// Lưu node_id vào EEPROM để tái sử dụng lần sau
void detectOrGenerateNodeId(HardwareSerial &lora_uart) {
  Serial.println("[DETECT] Listening for LoRa node_id (3 seconds)...");
  
  uint32_t start_time = millis();
  String buffer = "";
  
  while (millis() - start_time < 3000) {
    if (lora_uart.available()) {
      char c = lora_uart.read();
      buffer += c;
      
      // Look for pattern: "[LORA-NODE-ID] 123"
      if (buffer.indexOf("[LORA-NODE-ID]") >= 0) {
        int pos = buffer.indexOf("[LORA-NODE-ID]");
        int end = buffer.indexOf("\n", pos);
        if (end > 0) {
          String id_line = buffer.substring(pos, end);
          // Extract number from "[LORA-NODE-ID] 123"
          char* start_ptr = strchr(id_line.c_str(), ' ');
          if (start_ptr != NULL) {
            uint8_t node_id = (uint8_t)atoi(start_ptr + 1);
            Serial.printf("[DETECT] Found node_id from LoRa TX = %d\r\n", node_id);
            gVehicleConfig.setDeviceIdFromNodeId(node_id);
            // Save to EEPROM for next boot
            EEPROM.write(0, node_id);
            EEPROM.commit();
            Serial.printf("[DETECT] Saved node_id %d to EEPROM\r\n", node_id);
            return;  // ✅ Success
          }
        }
      }
      
      // Prevent buffer overflow
      if (buffer.length() > 256) {
        buffer = buffer.substring(128);
      }
    }
    delayMicroseconds(100);
  }
  
  // ===== FALLBACK 1: Check EEPROM for saved node_id =====
  uint8_t saved_node_id = EEPROM.read(0);
  if (saved_node_id != 0 && saved_node_id != 0xFF) {
    Serial.printf("[DETECT] Found saved node_id in EEPROM = %d\r\n", saved_node_id);
    gVehicleConfig.setDeviceIdFromNodeId(saved_node_id);
    return;  // ✅ Use saved ID
  }
  
  // ===== FALLBACK 2: Generate from ESP32 Chip ID & save to EEPROM =====
  Serial.println("[DETECT] LoRa node_id timeout, generating from Chip ID...");
  
  uint8_t node_id = 0;
  
  // Read MAC address (6 bytes) from factory efuse
  uint8_t chipId_bytes[6] = {0};
  esp_read_mac(chipId_bytes, ESP_MAC_WIFI_STA);
  
  // Derive unique Node ID from MAC bytes by XORing all
  for (int i = 0; i < 6; i++) {
    node_id ^= chipId_bytes[i];
  }
  
  if (node_id == 0) node_id = 1;  // Avoid 0
  
  Serial.printf("[DETECT] Generated node_id from Chip ID = %d\r\n", node_id);
  // Save to EEPROM for next boot
  EEPROM.write(0, node_id);
  EEPROM.commit();
  Serial.printf("[DETECT] Saved node_id %d to EEPROM\r\n", node_id);
  
  gVehicleConfig.setDeviceIdFromNodeId(node_id);
}

// ---- Task: Monitor tamper and send alert if detected ----
void TaskTamperMonitor(void *pvParameters) {
  for (;;) {
    // Check if tamper detected
    bool tamper = ldr.isTamper();
    
    if (tamper && !g_tamper_alert) {
      g_tamper_alert = true;
      uint16_t light_level = ldr.getLightLevel();
      
      Serial.printf("\r\n");
      Serial.printf("╔══════════════════════════════════════════════════════╗\r\n");
      Serial.printf("║         ⚠️  TAMPER ALERT - BOX OPENED!  ⚠️           ║\r\n");
      Serial.printf("║  Vehicle:  %s                          ║\r\n", gVehicleConfig.getDeviceId());
      Serial.printf("║  Time:     %lu ms                              ║\r\n", (unsigned long)millis());
      Serial.printf("║  Light:    %u (THRESHOLD: 500)                    ║\r\n", light_level);
      Serial.printf("╚══════════════════════════════════════════════════════╝\r\n");
      Serial.printf("\r\n");
    }
    
    // Monitor every 100ms
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---- LED heartbeat task ----
void TaskLED(void *pvParameters) {
  pinMode(LED_PIN, OUTPUT);
  bool on = false;
  for (;;) {
    // If tamper detected, blink faster (100ms)
    uint16_t blink_period = g_tamper_alert ? 100 : 5000;
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    on = !on;
    vTaskDelay(pdMS_TO_TICKS(blink_period));
  }
}

// ---- Sender task: read sensors, build JSON, write to LORA_SER ----
void TaskLoraSend(void *pv) {
  // LORA_SER already initialized in setup() - just wait a bit for stabilization
  delay(100);
  while (LORA_SER.available()) LORA_SER.read();

  // only print debug to Serial every N sends to reduce terminal spam
  const uint8_t DEBUG_PRINT_EVERY_N = 10; // change as needed
  
  uint32_t last_tamper_alert_sent = 0;

  for (;;) {
    // read sensors
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    float ax = NAN, ay = NAN, az = NAN;
    adxl.read(ax, ay, az);
    
    // read tamper/light status
    uint16_t light_level = ldr.readSmoothed();
    bool is_tamper = ldr.getTamperState();

    // read GPS from shared sensorData struct (thread-safe)
    double lat = 0.0, lng = 0.0;
    uint32_t sats = 0;
    if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      lat = sensorData.lat;
      lng = sensorData.lng;
      sats = sensorData.sats;
      xSemaphoreGive(sensorDataMutex);
    }

    // build JSON payload for LoRa
    uint32_t ts = millis();
    
    // ===== TESTING: Always send full telemetry (ignore tamper for now) =====
    char payload[512];
    int n = snprintf(payload, sizeof(payload),
      "{\"vehicle_id\":\"%s\",\"timestamp\":%lu,\"temp\":%.2f,\"humidity\":%.2f,\"battery\":4.0,\"gps\":{\"lat\":%.4f,\"lng\":%.4f},\"tamper\":%d,\"light_level\":%u,\"accel_mag\":%.2f}\n",
      gVehicleConfig.getDeviceId(),
      (unsigned long)ts,
      isnan(temp) ? -999.0F : temp,
      isnan(hum) ? -999.0F : hum,
      lat,     // From sensorData (thread-safe)
      lng,     // From sensorData (thread-safe)
      is_tamper ? 1 : 0,
      light_level,
      isnan(ax) ? -999.0F : sqrt(ax*ax + ay*ay + az*az)  // magnitude (fixed: was ax*ax + ax*ax + ax*ax)
    );
    
    // write to LoRa module UART
    if (n > 0) {
      int written = LORA_SER.write((uint8_t*)payload, n);
      LORA_SER.flush();
      
      Serial.printf("[ESP32->LORA] Written %d bytes to UART1: ", written);
      Serial.write((uint8_t*)payload, n);
      Serial.printf("\r\n");
    } else {
      Serial.printf("[ERROR] snprintf failed! is_tamper=%d, n=%d\r\n", is_tamper, n);
    }
    
    _seq++;

    // wait interval (can be updated by other mechanism if needed)
    vTaskDelay(pdMS_TO_TICKS(g_send_interval_ms));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Initialize EEPROM (needed for stable node_id storage)
  EEPROM.begin(512);
  
  Serial.printf("\r\n");
  Serial.printf("╔═══════════════════════════════════════════════════════╗\r\n");
  Serial.printf("║   AMMO TRANSPORT MONITORING SYSTEM - NODE STARTUP    ║\r\n");
  Serial.printf("║              (Phase 2: Multi-Vehicle)                ║\r\n");
  Serial.printf("╚═══════════════════════════════════════════════════════╝\r\n");
  Serial.printf("\r\n");
  
  Serial.println("[INIT] Initializing modules...");

  // Create global sensor data mutex (CRITICAL for thread safety!)
  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == NULL) {
    Serial.println("[ERROR] Failed to create sensorDataMutex!");
  }

  // Initialize vehicle configuration (loads from EEPROM or uses default)
  gVehicleConfig.begin();
  Serial.printf("[INIT] Vehicle ID (default): %s\r\n", gVehicleConfig.getDeviceId());

  // I2C for ADXL345
  Wire.begin();

  // sensor init
  dht.begin();
  if (!adxl.begin()) {
    Serial.println("[WARN] ADXL345 not found (check wiring)");
  }
  
  // === Initialize LoRa UART early and detect node_id ===
  Serial.println("[INIT] Starting LoRa UART...");
  LORA_SER.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(100);
  while (LORA_SER.available()) LORA_SER.read();  // flush
  
  // Listen for LoRa TX bootstrap message with node_id (max 3 seconds), or generate from Chip ID
  Serial.println("[INIT] Detecting/generating node_id...");
  detectOrGenerateNodeId(LORA_SER);
  Serial.printf("[INIT] Final Vehicle ID: %s\r\n", gVehicleConfig.getDeviceId());
  
  // LDR (Light Dependent Resistor) for tamper detection
  ldr.begin();
  ldr.setTamperThreshold(500);  // Adjust based on your environment
  Serial.println("[INIT] LDR tamper detection initialized (threshold=500)");

  // Tasks
  xTaskCreate(TaskLED,              "LED",        1024, NULL, 1, NULL);
  xTaskCreate(TaskTamperMonitor,    "TamperMon",  2048, NULL, 2, NULL);
  
  startDhtTask(2048, 1);        // DHT11 background task
  startAdxlTelemetry(4096, 1);  // ADXL345 telemetry task

  // create Lora sender task
  xTaskCreate(TaskLoraSend, "LoraSend", 4096, NULL, 1, NULL);

  // GPS
  gps.begin();
  
  Serial.printf("\r\n[INIT] All systems initialized\r\n");
  Serial.printf("[INIT] Starting patrol mode...\r\n");
  Serial.printf("\r\n");
}

void loop() {
  // ---- Cập nhật GPS ----
  gps.read();

  // Save GPS data to shared struct if location updated
  if (gps.updated()) {
    if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sensorData.lat = gps.latitude();
      sensorData.lng = gps.longitude();
      sensorData.sats = gps.satellites();
      sensorData.speed = gps.gpsObject().speed.kmph();
      xSemaphoreGive(sensorDataMutex);
      
      // Debug: Print when GPS updates (less spam)
      static uint32_t last_gps_update = 0;
      uint32_t now = millis();
      if (now - last_gps_update >= 5000) {  // Print every 5s
        Serial.printf("[GPS] Fix: %.4f, %.4f (sats:%lu speed:%.1f km/h)\r\n",
          sensorData.lat, sensorData.lng, sensorData.sats, sensorData.speed);
        last_gps_update = now;
      }
    }
  }

  // yield to tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}