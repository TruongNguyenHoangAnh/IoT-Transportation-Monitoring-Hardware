#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>

#include "modules/gps.h"
#include "modules/dht11.h"
#include "modules/adxl345.h"
#include "modules/local_memory.h"   // SD logging
// #include "modules/lora.h"      // <-- bỏ wrapper LoRa AT (không cần)

//
// ===== Pins / Config =====
#define DHTPIN    14
#define DHTTYPE   DHT11
#define LED_PIN   2

// // I2C (ADXL345)
// #define I2C_SDA   21
// #define I2C_SCL   22

// GPS: UART2 RX=16, TX=17 @ 9600
GPSNeo6M  gps(16, 17, 9600);
DHTModule dht(DHTPIN, DHTTYPE);
ADXLModule adxl; // ADXL345

// --- LoRa UART (RA-08H TX connected here) on UART1 ---
#define LORA_RX   25     // ESP32 RX1  <= TX of RA-08H
#define LORA_TX   26     // ESP32 TX1  => RX of RA-08H
#define LORA_BAUD 115200 // match pingpong_tx UART baud

HardwareSerial LORA_SER(1); // UART1

volatile uint32_t g_send_interval_ms = 2000; // mặc định 60s (giảm tần suất gửi/print)
static uint32_t _seq = 0;

// ---- LED heartbeat task ----
void TaskLED(void *pvParameters) {
  pinMode(LED_PIN, OUTPUT);
  bool on = false;
  for (;;) {
    digitalWrite(LED_PIN, on ? HIGH : LOW);
    on = !on;
    // giảm tần suất nhấp LED để giảm output/hoạt động
    vTaskDelay(pdMS_TO_TICKS(5000)); // 5s
  }
}

// ---- Sender task: read sensors, build JSON, write to LORA_SER ----
void TaskLoraSend(void *pv) {
  // init LORA UART (ESP32 UART1: rxPin, txPin)
  LORA_SER.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(50);
  // flush if any
  while (LORA_SER.available()) LORA_SER.read();

  // only print debug to Serial every N sends to reduce terminal spam
  const uint8_t DEBUG_PRINT_EVERY_N = 10; // change as needed

  for (;;) {
    // read sensors
    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    float ax = NAN, ay = NAN, az = NAN;
    adxl.read(ax, ay, az);

    // build JSON payload for LoRa
    uint32_t ts = millis();
    char payload[256];
    int n = snprintf(payload, sizeof(payload),
      "{\"device_id\":\"Ra-08H-Node1\",\"timestamp\":%lu,\"temp\":%.2f,\"battery\":%.2f,\"gps\":{\"lat\":%.4f,\"lng\":%.4f},\"rssi_lora\":%d}\n",
      (unsigned long)ts,
      isnan(temp) ? -999.0F : temp,
      4.0, // Replace with actual battery voltage if available
      gps.latitude(),
      gps.longitude(),
      -73 // Replace with actual RSSI value if available
    );

    // write to LoRa module UART
    LORA_SER.write((uint8_t*)payload, n);
    LORA_SER.flush(); // optional

    // debug on main Serial
    if ((DEBUG_PRINT_EVERY_N > 0) && ((_seq % DEBUG_PRINT_EVERY_N) == 0)) {
      Serial.print("[ESP32->LORA] "); Serial.print(payload);
    }

    // wait interval (can be updated by other mechanism if needed)
    vTaskDelay(pdMS_TO_TICKS(g_send_interval_ms));
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("DATN: initializing modules...");

  // I2C cho ADXL345
  Wire.begin();

  // sensor init
  dht.begin();
  if (!adxl.begin()) {
    Serial.println("Warning: ADXL345 not found (check wiring)");
  }

  // Tasks
  xTaskCreate(TaskLED,         "LED",        1024, NULL, 1, NULL);
  startDhtTask(2048, 1);        // nếu bạn có
  startAdxlTelemetry(4096, 1);  // nếu bạn có

  // create Lora sender task
  xTaskCreate(TaskLoraSend, "LoraSend", 4096, NULL, 1, NULL);

  // GPS
  gps.begin();
}

void loop() {
  // ---- Cập nhật GPS ----
  gps.read();

  // ---- Debug print GPS occasionally (ví dụ mỗi 60s) ----
  static uint32_t last_gps_print = 0;
  uint32_t now = millis();
  if (now - last_gps_print >= 60000) { // 60000 ms = 60s
    gps.printLocation();
    last_gps_print = now;
  }

  // yield to tasks
  vTaskDelay(pdMS_TO_TICKS(1000));
}
