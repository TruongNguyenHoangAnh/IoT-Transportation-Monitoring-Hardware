#include "dht11.h"

#include <WiFi.h>
#include <PubSubClient.h>

// ===== (Tùy chọn) Kết nối WiFi + MQTT (demo) =====
#ifndef WIFI_SSID
#define WIFI_SSID    "H3-908-5G"
#define WIFI_PASS    "66668888"
#define MQTT_BROKER  "192.168.1.100"
#define MQTT_PORT    1883
#define MQTT_TOPIC   "fleet/VX-21/MAIN_BAY/telemetry"
#endif

// ===== Định danh thiết bị (đưa vào JSON) =====
static const char* VEHICLE_ID = "VX-21";
static const char* DEVICE_ID  = "ESP32-AMMO-001";
static const char* COMPARTMENT= "MAIN_BAY";

// ===== Ngưỡng & chính sách =====
static const float AMMO_TEMP_HIGH_C  = 30.0f; // vào cảnh báo nhiệt
static const float AMMO_TEMP_CLEAR_C = 29.0f; // thoát cảnh báo nhiệt (hysteresis)
static const float AMMO_HUM_HIGH_P   = 80.0f; // vào cảnh báo ẩm
static const float AMMO_HUM_CLEAR_P  = 75.0f; // thoát cảnh báo ẩm (hysteresis)

// Bỏ qua N mẫu đầu (tránh số "rác" khi cảm biến vừa khởi động)
static const int WARMUP_SAMPLES = 2;

// Throttle: in/publish cảnh báo tối đa 1 lần mỗi khoảng này (ms)
static const unsigned long ALERT_MIN_INTERVAL_MS = 30000UL;

// ===== Trạng thái cảnh báo =====
static bool thermalAlertActive  = false;
static bool humidityAlertActive = false;
static unsigned long lastAlertMs = 0;

// Đếm mẫu để warmup
static int sampleCount = 0;

// ===== WiFi/MQTT (demo) =====
static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

// The project defines a global `DHTModule dht` in main.cpp. Use that instance here.
extern DHTModule dht;

void wifiConnectIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (strlen(WIFI_SSID) == 0) return; // không cấu hình -> bỏ qua
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
    delay(200);
  }
}

void mqttConnectIfNeeded() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.connect(DEVICE_ID);
}

// ===== In/Pub tiện ích =====
void publishIfConnected(const String& payload) {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC, payload.c_str());
  }
}

void printTelemetryHuman(float t, float h, const char* status) {
  Serial.print("[AMMO-TELEMETRY] temp_c=");
  Serial.print(t, 1);
  Serial.print(", hum_pct=");
  Serial.print(h, 1);
  Serial.print(", compartment=");
  Serial.print(COMPARTMENT);
  Serial.print(", status=");
  Serial.println(status);
}

void printTelemetryJSON(float t, float h, const char* status) {
  // timestamp dùng millis() để đơn giản; có thể thay bằng epoch (NTP) khi cần
  unsigned long ts = millis();
  String json = "{";
  json += "\"type\":\"telemetry\"";
  json += ",\"domain\":\"ammo_transport\"";
  json += ",\"vehicle_id\":\"" + String(VEHICLE_ID) + "\"";
  json += ",\"device_id\":\""  + String(DEVICE_ID)  + "\"";
  json += ",\"compartment\":\""+ String(COMPARTMENT)+"\"";
  json += ",\"timestamp_ms\":" + String(ts);
  json += ",\"temp_c\":"       + String(t, 2);
  json += ",\"hum_pct\":"      + String(h, 2);
  json += ",\"status\":\""     + String(status) + "\"";
  // Gợi ý: server có thể tính "risk" dựa trên status + rule engine
  json += ",\"risk\":\"eval\"";
  json += "}";
  Serial.println(json);
  publishIfConnected(json);
}

void maybePrintAlerts(float t, float h, bool edgeThermal, bool edgeHumidity) {
  const unsigned long now = millis();
  const bool timeOk = (now - lastAlertMs) >= ALERT_MIN_INTERVAL_MS;

  // In cảnh báo nếu: vừa chuyển trạng thái vào ALERT (edge), hoặc đến kỳ throttle
  if ((thermalAlertActive && (edgeThermal || timeOk))) {
    Serial.print("[AMMO-ALERT] Thermal risk: temp_c=");
    Serial.print(t, 1);
    Serial.println(" exceeds safe threshold for ammunition transport.");
    lastAlertMs = now;
  }
  if ((humidityAlertActive && (edgeHumidity || timeOk))) {
    Serial.print("[AMMO-ALERT] Humidity risk: hum_pct=");
    Serial.print(h, 1);
    Serial.println(" exceeds safe threshold for ammunition storage.");
    lastAlertMs = now;
  }
}

// ===== Task đọc DHT11 =====
void TaskDHT11(void *pvParameters) {
  for (;;) {
    float h = dht.readHumidity();
    float t = dht.readTemperature(); // °C

    // Đảm bảo MQTT "nhịp tim" nếu có cấu hình
    if (WiFi.status() == WL_CONNECTED) mqttClient.loop();

    if (isnan(h) || isnan(t)) {
      Serial.println("[AMMO-ERROR] DHT offline or read failure – unable to verify ammunition cargo environment.");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // Bỏ qua N mẫu đầu (warmup)
    if (sampleCount < WARMUP_SAMPLES) {
      sampleCount++;
      // In JSON để quan sát (tùy chọn), hoặc chỉ log nhẹ:
      // Serial.println("[AMMO-SYSTEM] Warming up sensor, skipping this reading.");
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // ==== Hysteresis cho THERMAL ====
    bool prevThermal = thermalAlertActive;
    if (!thermalAlertActive && t > AMMO_TEMP_HIGH_C) {
      thermalAlertActive = true;
    } else if (thermalAlertActive && t < AMMO_TEMP_CLEAR_C) {
      thermalAlertActive = false;
    }

    // ==== Hysteresis cho HUMIDITY ====
    bool prevHumidity = humidityAlertActive;
    if (!humidityAlertActive && h > AMMO_HUM_HIGH_P) {
      humidityAlertActive = true;
    } else if (humidityAlertActive && h < AMMO_HUM_CLEAR_P) {
      humidityAlertActive = false;
    }

    // ==== Tính status tổng hợp cho TELEMETRY/JSON ====
    const char* status = (thermalAlertActive || humidityAlertActive) ? "ALERT" : "OK";

    // 1) Log người đọc
    printTelemetryHuman(t, h, status);

    // 2) JSON cho máy đọc + publish MQTT (nếu có)
    printTelemetryJSON(t, h, status);

    // 3) In cảnh báo (throttle + edge trigger)
    bool edgeThermal  = (!prevThermal  && thermalAlertActive);
    bool edgeHumidity = (!prevHumidity && humidityAlertActive);
    maybePrintAlerts(t, h, edgeThermal, edgeHumidity);

    vTaskDelay(pdMS_TO_TICKS(5000)); // 5s
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
