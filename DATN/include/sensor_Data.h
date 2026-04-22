#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct SensorData {
    double lat;              // GPS latitude
    double lng;              // GPS longitude
    uint32_t sats;           // satellite count
    float speed;             // GPS speed (km/h)
    float temp;              // temperature (°C)
    float hum;               // humidity (%)
    float accel;            // acceleration magnitude in g
    bool shock_detected;     // ADXL345 shock
    bool is_moving;          // motion flag
};

// Global sensor data + mutex
extern SensorData sensorData;
extern SemaphoreHandle_t sensorDataMutex;

#endif
