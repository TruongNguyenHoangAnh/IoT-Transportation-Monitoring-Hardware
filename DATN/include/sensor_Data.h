#ifndef SENSOR_DATA_H
#define SENSOR_DATA_H

#include <Arduino.h>

struct SensorData {
    // GPS Data
    double lat = 0.0;
    double lng = 0.0;
    uint32_t sats = 0;
    float speed = 0.0; 

    //Environmental Data
    float temp = 0.0;
    float hum = 0.0;

    //Motion Data
    float accel_mag = false;
    bool shock_detected = false;
    bool is_moving = false;

    //System
    uint32_t timestamp = 0;
};

// instance Global and Mutex
extern SensorData sensorData;
extern SemaphoreHandle_t sysDataMutex;

#endif 