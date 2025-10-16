#include "adxl345.h"

ADXLModule::ADXLModule() {}

bool ADXLModule::begin() {
  if (!accel.begin()) {
    return false;
  }
  accel.setRange(ADXL345_RANGE_16_G);
  return true;
}

bool ADXLModule::read(float &x, float &y, float &z) {
  sensors_event_t event;
  accel.getEvent(&event);
  if (isnan(event.acceleration.x) || isnan(event.acceleration.y) || isnan(event.acceleration.z)) return false;
  x = event.acceleration.x;
  y = event.acceleration.y;
  z = event.acceleration.z;
  return true;
}

// ---------------- I2C register helpers and telemetry task (inline, non-class) ----------------
// Constants
static const uint8_t DEVICE_ADDRESS = 0x53; // ADXL345 I2C
static const uint8_t REG_DATA_FORMAT = 0x31;
static const uint8_t REG_POWER_CTRL  = 0x2D;
static const uint8_t REG_INT_ENABLE  = 0x2E;

static const uint8_t REG_DATAX0 = 0x32;
static const uint8_t REG_DATAX1 = 0x33;
static const uint8_t REG_DATAY0 = 0x34;
static const uint8_t REG_DATAY1 = 0x35;
static const uint8_t REG_DATAZ0 = 0x36;
static const uint8_t REG_DATAZ1 = 0x37;

int16_t raw_x = 0, raw_y = 0, raw_z = 0;
const float G_PER_LSB = 0.0039f; // full resolution ~4mg/LSB
const float SHOCK_G_THRESHOLD = 2.5f;
const unsigned long TELEMETRY_INTERVAL_MS = 5000;
static unsigned long last_ts = 0;

// I2C helpers
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

static void readXYZ_raw() {
  uint8_t buf[6];
  readRegister(DEVICE_ADDRESS, REG_DATAX0, 6, buf);
  raw_x = (int16_t)((int16_t)buf[1] << 8 | buf[0]);
  raw_y = (int16_t)((int16_t)buf[3] << 8 | buf[2]);
  raw_z = (int16_t)((int16_t)buf[5] << 8 | buf[4]);
}

// Telemetry task to be created externally if desired
void TaskADXL_Telem(void *pvParameters) {
  Wire.begin();
  // init ADXL345 via I2C raw registers as fallback
  writeRegister(DEVICE_ADDRESS, REG_DATA_FORMAT, 0x0B); // FULL_RES=1, Range=±16g
  writeRegister(DEVICE_ADDRESS, REG_POWER_CTRL,  0x08); // Measure=1
  writeRegister(DEVICE_ADDRESS, REG_INT_ENABLE,  0x80); // Data Ready int (optional)

  Serial.println("TELEM,ts,x,y,z,shock");

  for (;;) {
    unsigned long now = millis();
    if (now - last_ts >= TELEMETRY_INTERVAL_MS) {
      last_ts = now;
      readXYZ_raw();
      float xg = raw_x * G_PER_LSB;
      float yg = raw_y * G_PER_LSB;
      float zg = raw_z * G_PER_LSB;
      float mag_g = sqrtf(xg*xg + yg*yg + zg*zg);
      int shock = (mag_g >= SHOCK_G_THRESHOLD) ? 1 : 0;
      Serial.print("TELEM,");
      Serial.print(now); Serial.print(",");
      Serial.print(raw_x); Serial.print(",");
      Serial.print(raw_y); Serial.print(",");
      Serial.print(raw_z); Serial.print(",");
      Serial.println(shock);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Helper to start telemetry task
void startAdxlTelemetry(unsigned long stackSize, UBaseType_t priority) {
  xTaskCreate(TaskADXL_Telem, "ADXL_Telem", stackSize, NULL, priority, NULL);
}
