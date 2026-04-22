#include <Arduino.h>
#include "security.h"

// Uncomment to enable ADXL data capture mode for labelled dataset collection
// #define CAPTURE_ADXL_DATA 1

#include <Wire.h>
#include <TinyGPSPlus.h>
#include <esp_system.h>   // For chip ID functions

#include "gps.h"
#include "dht11.h"
#include "adxl345.h"
#include "ldr.h"
#include "vehicle_config.h"
// #include "local_memory.h"   // SD logging - DISABLED (not needed for real-time telemetry)
#include "sensor_Data.h"    // Shared sensor data struct + mutex
// #include "lora.h"      // <-- bỏ wrapper LoRa AT (không cần)

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

// ---- Helper: Auto-increment node_id from EEPROM counter ----
// Each device gets unique Transport-X (1-100) on first boot
// Counter stored in EEPROM byte 0 tracks next available node_id
// Magic byte at EEPROM byte 1 detects if using new auto-increment system
// 
// ⚠️ OVERRIDE: Set FORCE_NODE_ID to 0 to use auto-increment, or 1-100 to force specific node
#define FORCE_NODE_ID 2  // Set to 1 for Transport-1, 2 for Transport-2, etc. | 0 = auto-increment from EEPROM

void detectOrGenerateNodeId(HardwareSerial &lora_uart) {
  (void)lora_uart;  // Unused parameter (TX no longer broadcasts node_id text)
  
  // ⚠️ DEBUG: Override for testing specific nodes
  if (FORCE_NODE_ID > 0 && FORCE_NODE_ID <= 100) {
    Serial.printf("[SYNC] FORCE_NODE_ID enabled - Using Transport-%d (override mode)\r\n", FORCE_NODE_ID);
    gVehicleConfig.setDeviceIdFromNodeId(FORCE_NODE_ID);
    return;
  }
  
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

static uint32_t getVehicleLoraSendDelayMs() {
  uint8_t veh_num = gVehicleConfig.getVehicleNumber();
  const uint32_t slot_ms = 250;
  static const uint8_t slot_order[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };

  if (veh_num == 0) {
    return 0;
  }

  uint8_t slot_index = (veh_num - 1) % 8;
  return slot_order[slot_index] * slot_ms;
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
      Serial.printf("[TAMPER] Light Level: %u (THRESHOLD: 850)\r\n", light_level);
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

  // Apply startup offset for Transport-1 / Transport-2 to avoid collision
  uint32_t send_delay = getVehicleLoraSendDelayMs();
  if (send_delay > 0) {
    Serial.printf("[SYNC] Transport-%u delayed send start by %lums\r\n", gVehicleConfig.getVehicleNumber(), (unsigned long)send_delay);
    vTaskDelay(pdMS_TO_TICKS(send_delay));
  }

  // only print debug to Serial every N sends to reduce terminal spam
  const uint8_t DEBUG_PRINT_EVERY_N = 10; // change as needed
  
  uint32_t last_tamper_alert_sent = 0;

  for (;;) {
    // read sensor values from shared sensorData struct (thread-safe)
    float temp = -999.0f;
    float hum = -999.0f;
    float accel_g = -999.0f;
    bool adxl_ok = false;
    bool shock = false;
    bool is_moving = false;

    if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      temp = sensorData.temp;
      hum = sensorData.hum;
      accel_g = sensorData.accel;
      shock = sensorData.shock_detected;
      is_moving = sensorData.is_moving;
      xSemaphoreGive(sensorDataMutex);
      adxl_ok = (accel_g > -900.0f);
    }

    // ⚠️ Error handling: If sensor fails, set to error sentinel (-999)
    if (isnan(temp) || temp < -100 || temp > 150) {
      Serial.println("[DHT11-ERROR] Sensor read failed (no valid data)");
      temp = -999.0f;
    }
    if (isnan(hum) || hum < 0 || hum > 100) {
      hum = -999.0f;
    }
    if (!adxl_ok) {
      Serial.println("[ADXL345-ERROR] Sensor data unavailable or not ready");
      accel_g = -999.0f;
    }
    
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

    // build minified JSON payload for LoRa
    uint32_t ts = millis();
    // uint32_t seq = tx_sequence; // nếu có biến seq, lấy đúng thứ tự gói
    char payload[320];
    int n = snprintf(payload, sizeof(payload),
      "{\"v\":\"%s\",\"ts\":%lu,\"t\":%.1f,\"h\":%.1f,\"a\":%.2f,\"l\":%u,\"x\":%d,\"la\":%.5f,\"lo\":%.5f}",
      gVehicleConfig.getDeviceId(),
      (unsigned long)ts,
      isnan(temp) ? -999.0F : temp,
      isnan(hum) ? -999.0F : hum,
      (accel_g < -900.0f) ? -999.0F : accel_g,
      light_level,
      is_tamper ? 1 : 0,
      lat,
      lng
    );
    

    // ===== MÃ HÓA HMAC-SHA256 lên JSON payload =====
    if (n > 0) {
      String jsonPayload(payload);
      String signature = hmacSha256(jsonPayload);
      String signedJson = jsonPayload;
      if (signedJson.endsWith("}")) {
        signedJson = signedJson.substring(0, signedJson.length() - 1);
      }
      signedJson += ",\"sig\":\"" + signature + "\"}";

      // ===== MÃ HÓA AES-128 CBC + BASE64 trước khi gửi =====
      String securePayload = encryptDataToAESBase64(signedJson);
      LORA_SER.println(securePayload); // Gửi qua UART dạng base64 an toàn
      LORA_SER.flush();
      Serial.print("[ESP32->LORA] AES-128 CBC + BASE64 payload sent (len: ");
      Serial.print(securePayload.length());
      Serial.println(")");
    } else {
      Serial.printf("[ERROR] snprintf failed! n=%d\r\n", n);
    }

    vTaskDelay(pdMS_TO_TICKS(g_send_interval_ms));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  EEPROM.begin(512); 
  Serial.println("[INIT] Initializing modules...");

  sensorDataMutex = xSemaphoreCreateMutex();
  if (sensorDataMutex == NULL) {
    Serial.println("[ERROR] Failed to create sensorDataMutex!");
  }

  // Initialize sensorData sentinel values before tasks start
  if (sensorDataMutex != NULL) {
    if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sensorData.temp = -999.0f;
      sensorData.hum = -999.0f;
      sensorData.accel = -999.0f;
      sensorData.shock_detected = false;
      sensorData.is_moving = false;
      xSemaphoreGive(sensorDataMutex);
    }
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
  ldr.setTamperThreshold(850);  // Adjusted for older sensor variants (was 500)
  Serial.println("[INIT] LDR tamper detection initialized (threshold=850)");

  // Tasks
  // xTaskCreate(TaskLED,              "LED",        1024, NULL, 1, NULL);  // DISABLED - not essential
  xTaskCreate(TaskTamperMonitor,    "TamperMon",  2048, NULL, 2, NULL);
  
  startDhtTask(2048, 1);        // DHT11 background task
  startAdxlTelemetry(4096, 1);  // ADXL345 background task to populate shared sensorData

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
  // gps.printDebugStats();  // ⚠️ DEBUG: Disabled - clean output for production

  // Save GPS data to shared struct if location updated
  if (gps.updated()) {
    if (xSemaphoreTake(sensorDataMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      sensorData.lat = gps.latitude();
      sensorData.lng = gps.longitude();
      sensorData.sats = gps.satellites();
      sensorData.speed = gps.gpsObject().speed.kmph();
      xSemaphoreGive(sensorDataMutex);
    }
  }

  // yield to tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}