#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <esp_system.h>   // For chip ID functions

#include "modules/gps.h"
#include "modules/dht11.h"
#include "modules/adxl345.h"
#include "modules/ldr.h"
#include "modules/vehicle_config.h"
// #include "modules/local_memory.h"   // SD logging - DISABLED (not needed for real-time telemetry)
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

// ---- Helper: Auto-increment node_id from EEPROM counter ----
// Each device gets unique Transport-X (1-100) on first boot
// Counter stored in EEPROM byte 0 tracks next available node_id
// Magic byte at EEPROM byte 1 detects if using new auto-increment system
void detectOrGenerateNodeId(HardwareSerial &lora_uart) {
  (void)lora_uart;  // Unused parameter (TX no longer broadcasts node_id text)
  
  Serial.println("[SYNC] Auto-assigning node_id from EEPROM counter...");
  
  // Check magic byte to detect if using new auto-increment system
  const uint8_t MAGIC_BYTE = 0xAA;
  uint8_t magic = EEPROM.read(1);
  
  // If magic byte not set, this is old EEPROM data - reset counter
  if (magic != MAGIC_BYTE) {
    Serial.println("[SYNC] Old EEPROM detected - resetting counter to 0");
    EEPROM.write(0, 0);
    EEPROM.write(1, MAGIC_BYTE);
    EEPROM.commit();
  }
  
  // Read counter from EEPROM byte 0
  uint8_t counter = EEPROM.read(0);
  
  // First boot: counter = 0, start from 1
  if (counter == 0 || counter == 0xFF) {
    counter = 1;
  }
  
  uint8_t node_id = counter;
  
  // Increment counter for next device
  counter++;
  if (counter > 100) counter = 1;  // Wrap around after 100
  
  // Save incremented counter for next device
  EEPROM.write(0, counter);
  EEPROM.write(1, MAGIC_BYTE);  // Keep magic byte
  EEPROM.commit();
  
  Serial.printf("[SYNC] Assigned node_id = %u (Transport-%u)\r\n", node_id, node_id);
  Serial.printf("[SYNC] Next device will get node_id = %u\r\n", counter);
  
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
      
      Serial.println("[TAMPER ALERT] BOX OPENED!");
      Serial.printf("[TAMPER] Vehicle: %s\r\n", gVehicleConfig.getDeviceId());
      Serial.printf("[TAMPER] Time: %lu ms\r\n", (unsigned long)millis());
      Serial.printf("[TAMPER] Light Level: %u (THRESHOLD: 500)\r\n", light_level);
    }
    
    // Monitor every 100ms
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ===== DISABLED: LED heartbeat task (not essential - visual indicator only) =====
// void TaskLED(void *pvParameters) {
//   pinMode(LED_PIN, OUTPUT);
//   bool on = false;
//   for (;;) {
//     // If tamper detected, blink faster (100ms)
//     uint16_t blink_period = g_tamper_alert ? 100 : 5000;
//     digitalWrite(LED_PIN, on ? HIGH : LOW);
//     on = !on;
//     vTaskDelay(pdMS_TO_TICKS(blink_period));
//   }
// }

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
    
    // ===== Build clean JSON telemetry (one line) =====
    char payload[512];
    int n = snprintf(payload, sizeof(payload),
      "{\"vehicle_id\":\"%s\",\"timestamp\":%lu,\"seq\":%lu,\"temp\":%.2f,\"humidity\":%.2f,\"gps\":{\"lat\":%.4f,\"lng\":%.4f},\"tamper\":%d,\"light_level\":%u,\"accel_mag\":%.2f}\n",
      gVehicleConfig.getDeviceId(),
      (unsigned long)ts,
      (unsigned long)_seq,
      isnan(temp) ? -999.0F : temp,
      isnan(hum) ? -999.0F : hum,
      lat,
      lng,
      is_tamper ? 1 : 0,
      light_level,
      isnan(ax) ? -999.0F : sqrt(ax*ax + ay*ay + az*az)
    );
    
    // write to LoRa module UART (LORA_SER on UART1)
    if (n > 0) {
      int written = LORA_SER.write((uint8_t*)payload, n);  // Send JSON to LoRa TX module via UART1
      LORA_SER.flush();
      
      // Debug output to Serial console (for monitoring, not LoRa)
      Serial.println("[ESP32->LORA] JSON sent to LoRa TX module (UART1):");
      Serial.println(payload);
    } else {
      Serial.printf("[ERROR] snprintf failed! n=%d\r\n", n);
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
  
// Serial.printf("\r\n");
  // Serial.printf("╔═══════════════════════════════════════════════════════╗\r\n");
  // Serial.printf("║   AMMO TRANSPORT MONITORING SYSTEM - NODE STARTUP    ║\r\n");
  // Serial.printf("║              (Phase 2: Multi-Vehicle)                ║\r\n");
  // Serial.printf("╚═══════════════════════════════════════════════════════╝\r\n");
  // Serial.printf("\r\n");
  
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
  // xTaskCreate(TaskLED,              "LED",        1024, NULL, 1, NULL);  // DISABLED - not essential
  xTaskCreate(TaskTamperMonitor,    "TamperMon",  2048, NULL, 2, NULL);
  
  startDhtTask(2048, 1);        // DHT11 background task
  // startAdxlTelemetry(4096, 1);  // ADXL345 telemetry task - DISABLED (not used for JSON)

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