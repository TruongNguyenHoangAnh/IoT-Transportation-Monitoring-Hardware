#include <Arduino.h>
#include <Wire.h>
#include <TinyGPSPlus.h>

#include "modules/gps.h"
#include "modules/dht11.h"
#include "modules/adxl345.h"
#include "modules/local_memory.h"   // SD logging
#include "include/sensor_Data.h"
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

struct __attribute__((packed)) LoraPacket {
    uint8_t device_id = 1;
    int32_t lat_fixed;
    int32_t lng_fixed;
    uint8_t temp;
    uint8_t hum;
    uint8_t flags;
    uint16_t msg_id;
};

bool waitResponse(const char* expected, uint32_t timeout) {
  uint32_t start = millis();
  String response = "";
  while (millis() - start < timeout) {
    while (LORA_SER.available()) {
      char c = LORA_SER.read();
      response += c;
    }
    if (response.indexOf(expected) != -1) {
      return true; // Tìm thấy chuỗi mong muốn (ví dụ "OK" hoặc "Done")
    }
    vTaskDelay(10);
  }
  return false; // Timeout
}

void sendCommandWithRetry(String cmd, int maxRetries) {
  for (int i = 1; i <= maxRetries; i++) {
    // 1. Xóa bộ đệm cũ
    while(LORA_SER.available()) LORA_SER.read();
    
    // 2. Gửi lệnh
    LORA_SER.println(cmd);
    Serial.printf("[LoRa] Sending attempt %d/%d...\n", i, maxRetries);

    // 3. Chờ xác nhận (Ví dụ chờ "OK" hoặc "TX DONE" trong 3 giây)
    // Tùy firmware, lệnh gửi thường trả về "OK" ngay, sau đó mới "TX DONE"
    if (waitResponse("OK", 3000)) {
       Serial.println("[LoRa] Send Command OK!");
       // Nếu muốn chắc chắn gói tin đi thành công thì chờ thêm "TX DONE"
       // if (waitResponse("TX DONE", 5000)) Serial.println("Packet Arrived!");
       return; // Gửi thành công, thoát hàm
    } else {
       Serial.println("[LoRa] Timeout/Error! Retrying...");
       vTaskDelay(1000 + random(500)); // Nghỉ 1 chút rồi thử lại (Backoff)
    }
  }
  Serial.println("[LoRa] FAILED after all retries.");
}

// ---- Task Gửi LoRa ----
void TaskLoraSend(void *pv) {
  LORA_SER.begin(LORA_BAUD, SERIAL_8N1, LORA_RX, LORA_TX);
  delay(1000);
  
  // Cấu hình ban đầu cho LoRa (chạy 1 lần)
  // sendCommandWithRetry("AT+CJOIN=1,0,10,8", 3); // Ví dụ lệnh Join

  LoraPacket pkt;
  uint16_t seq_counter = 0;

  for (;;) {
    bool hasData = false;

    // 1. LẤY DỮ LIỆU TỪ KHO
    if (xSemaphoreTake(sysDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        pkt.device_id = 1;
        pkt.lat_fixed = (int32_t)(sensorData.lat * 100000);
        pkt.lng_fixed = (int32_t)(sensorData.lng * 100000);
        pkt.temp      = (uint8_t)sensorData.temp;
        pkt.hum       = (uint8_t)sensorData.hum;
        
        pkt.flags = 0;
        if (sensorData.shock_detected) pkt.flags |= 1;
        if (sensorData.is_moving)      pkt.flags |= 2;
        
        sensorData.shock_detected = false; 
        pkt.msg_id = seq_counter++;
        
        hasData = true;
        xSemaphoreGive(sysDataMutex);
    }

    if (hasData) {
        // 2. CHUẨN BỊ CHUỖI HEX
        char hexBuffer[64];
        char *ptr = hexBuffer;
        uint8_t *bytes = (uint8_t*)&pkt;
        for (int i = 0; i < sizeof(LoraPacket); i++) {
            ptr += sprintf(ptr, "%02X", bytes[i]);
        }

        // 3. GỬI VỚI CƠ CHẾ RETRY (3 LẦN)
        // Lệnh: AT+DTRX=[confirm],[nbtrials],[len],<data>
        // nbtrials=1 (để mình tự quản lý retry bằng code này cho chắc)
        String cmd = "AT+DTRX=1,1,15," + String(hexBuffer); 
        
        sendCommandWithRetry(cmd, 3); 
    }

    vTaskDelay(pdMS_TO_TICKS(2000)); // Gửi mỗi 2 giây
  }
}

// ---- Các phần khác giữ nguyên ----
void TaskLED(void *pvParameters) {
  pinMode(LED_PIN, OUTPUT);
  for (;;) {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  
  sysDataMutex = xSemaphoreCreateMutex(); // QUAN TRỌNG
  
  Wire.begin();
  dht.begin();
  if (!adxl.begin()) Serial.println("ADXL error");
  gps.begin();

  xTaskCreate(TaskLED, "LED", 1024, NULL, 1, NULL);
  startDhtTask(2048, 1);
  startAdxlTelemetry(4096, 1);
  xTaskCreate(TaskLoraSend, "LoraSend", 4096, NULL, 1, NULL);
}

void loop() {
  gps.read();
  if (gps.gpsObject().location.isUpdated()) {
      if (xSemaphoreTake(sysDataMutex, 10) == pdTRUE) {
          sensorData.lat  = gps.latitude();
          sensorData.lng  = gps.longitude();
          sensorData.sats = gps.satellites();
          sensorData.speed = gps.gpsObject().speed.kmph();
          xSemaphoreGive(sysDataMutex);
      }
  }
  vTaskDelay(1);
}
