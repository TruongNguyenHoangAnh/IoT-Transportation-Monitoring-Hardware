#include "adxl345.h"
#include "vehicle_config.h"
#include <math.h>

// Declare external vehicle config (initialized in main.cpp)
extern VehicleConfig gVehicleConfig;

// ---------- ADXL345 I2C ----------
static const uint8_t DEVICE_ADDRESS = 0x53; // ALT ADDRESS = GND
static const uint8_t REG_DATA_FORMAT = 0x31;
static const uint8_t REG_POWER_CTRL  = 0x2D;
static const uint8_t REG_INT_ENABLE  = 0x2E;

static const uint8_t REG_DATAX0 = 0x32;
static const uint8_t REG_DATAX1 = 0x33;
static const uint8_t REG_DATAY0 = 0x34;
static const uint8_t REG_DATAY1 = 0x35;
static const uint8_t REG_DATAZ0 = 0x36;
static const uint8_t REG_DATAZ1 = 0x37;

static int16_t x = 0, y = 0, z = 0;

// Full-resolution: 4 mg/LSB ≈ 0.0039 g/LSB
static const float G_PER_LSB = 0.0039f;
// ===== DISABLED: Telemetry output and ammo-themed constants (not needed - magnitude used directly in JSON) =====
// static const float SHOCK_G_THRESHOLD = 2.5f;
// static const unsigned long TELEMETRY_INTERVAL_MS = 1000;
// static unsigned long last_ts = 0;
// static const char* DEVICE_ID    = "ESP32-AMMO-IMU-001";
// static const char* COMPARTMENT  = "MAIN_BAY";

// ---------- I2C helpers ----------
static void writeRegister(uint8_t device, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(device);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void readRegister(uint8_t device, uint8_t startReg, uint8_t numBytes, uint8_t *outValues) {
  Wire.beginTransmission(device);
  Wire.write(startReg);
  Wire.endTransmission();

  uint8_t received = Wire.requestFrom(device, numBytes);
  uint8_t i = 0;
  while (Wire.available() && i < received && i < numBytes) {
    outValues[i++] = Wire.read();
  }
}

// Đọc 6 byte và ghép thành x,y,z (signed 16-bit, little endian)
static void readXYZ() {
  uint8_t buf[6];
  readRegister(DEVICE_ADDRESS, REG_DATAX0, 6, buf);
  x = (int16_t)((int16_t)buf[1] << 8 | buf[0]);
  y = (int16_t)((int16_t)buf[3] << 8 | buf[2]);
  z = (int16_t)((int16_t)buf[5] << 8 | buf[4]);
}

// === Class implementation (using direct I2C) ===
ADXLModule::ADXLModule() {}

bool ADXLModule::begin() {
  // Try Adafruit lib first
  if (accel.begin()) {
    accel.setRange(ADXL345_RANGE_16_G);
    delay(50);  // Wait for Adafruit init
    return true;
  }

  // Fallback: direct I2C init
  Wire.begin();
  writeRegister(DEVICE_ADDRESS, REG_DATA_FORMAT, 0x0B); // FULL_RES=1, Range=±16g
  writeRegister(DEVICE_ADDRESS, REG_POWER_CTRL,  0x08); // Measure=1
  delay(100);  // Critical: wait for sensor to boot
  writeRegister(DEVICE_ADDRESS, REG_INT_ENABLE,  0x80); // Data Ready int (optional)
  return true;
}

bool ADXLModule::read(float &xg, float &yg, float &zg) {
  // Try Adafruit lib first
  sensors_event_t event;
  accel.getEvent(&event);
  if (!isnan(event.acceleration.x) && !isnan(event.acceleration.y) && !isnan(event.acceleration.z)) {
    xg = event.acceleration.x;
    yg = event.acceleration.y;
    zg = event.acceleration.z;
    return true;
  }

  // Fallback: direct I2C read
  // ⚠️ IMPORTANT: Check if I2C communication succeeded
  uint8_t test_buf[1];
  Wire.beginTransmission(DEVICE_ADDRESS);
  Wire.write(REG_DATAX0);
  if (Wire.endTransmission() != 0) {
    // I2C error - sensor not responding
    xg = -999.0f;
    yg = -999.0f;
    zg = -999.0f;
    return false;  // Signal error to caller
  }

  readXYZ();
  xg = x * G_PER_LSB;
  yg = y * G_PER_LSB;
  zg = z * G_PER_LSB;
  return true;
}

// Get raw LSB values
void ADXLModule::getRawLSB(int16_t &x_lsb, int16_t &y_lsb, int16_t &z_lsb) {
  readXYZ();
  x_lsb = x;
  y_lsb = y;
  z_lsb = z;
}

// ===== DISABLED: Telemetry task (not needed - TaskLoraSend reads ADXL directly for JSON) =====
// void TaskADXL_Telem(void *pvParameters) {
//   Wire.begin();
//   writeRegister(DEVICE_ADDRESS, REG_DATA_FORMAT, 0x0B); // FULL_RES=1, Range=±16g
//   writeRegister(DEVICE_ADDRESS, REG_POWER_CTRL,  0x08); // Measure=1
//   delay(100); // Wait for sensor to be ready (CRITICAL!)
//   writeRegister(DEVICE_ADDRESS, REG_INT_ENABLE,  0x80); // Data Ready int (optional)
//
//   for (;;) {
//     unsigned long now = millis();
//     if (now - last_ts >= TELEMETRY_INTERVAL_MS) {
//       last_ts = now;
//       readXYZ();
//       float xg = x * G_PER_LSB;
//       float yg = y * G_PER_LSB;
//       float zg = z * G_PER_LSB;
//       float mag_g = sqrtf(xg*xg + yg*yg + zg*zg);
//       bool shock = (mag_g >= SHOCK_G_THRESHOLD);
//       const char* status = shock ? "ALERT" : "OK";
//       
//       // JSON output
//       Serial.print("{\"type\":\"telemetry\",\"domain\":\"ammo_transport\"");
//       Serial.print(",\"vehicle_id\":\"" );   Serial.print(gVehicleConfig.getDeviceId());   Serial.print("\"");
//       Serial.print(",\"device_id\":\"");    Serial.print(DEVICE_ID);    Serial.print("\"");
//       Serial.print(",\"compartment\":\"");  Serial.print(COMPARTMENT);  Serial.print("\"");
//       Serial.print(",\"timestamp_ms\":");   Serial.print(now);
//       Serial.print(",\"accel\":{\"x_lsb\":"); Serial.print(x);
//       Serial.print(",\"y_lsb\":");            Serial.print(y);
//       Serial.print(",\"z_lsb\":");            Serial.print(z);
//       Serial.print(",\"mag_g\":");            Serial.print(mag_g, 3);
//       Serial.print("}");
//       Serial.print(",\"status\":\"");       Serial.print(status);       Serial.print("\"");
//       Serial.println("}");
//
//       if (shock) {
//         Serial.print("[AMMO-ALERT] Shock risk: |a|=");
//         Serial.print(mag_g, 2);
//         Serial.print(" g exceeds threshold ");
//         Serial.print(SHOCK_G_THRESHOLD, 2);
//         Serial.println(" g for ammunition transport.");
//       }
//     }
//     vTaskDelay(pdMS_TO_TICKS(10));
//   }
// }
//
// // Helper to start telemetry task
// void startAdxlTelemetry(unsigned long stackSize, UBaseType_t priority) {
//   xTaskCreate(TaskADXL_Telem, "ADXL_Telem", stackSize, NULL, priority, NULL);
// }
