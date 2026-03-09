#pragma once
#include <Arduino.h>

/**
 * LDR Module - Light Sensor for Tamper Detection
 * 
 * Hardware: Light Dependent Resistor (LDR) on GPIO 35 (ADC1_7)
 * Purpose: Detect when enclosure is opened (light increases)
 * 
 * Tamper Logic:
 * - Normal: Light ~ 0-500 (sealed box)
 * - Tamper: Light > TAMPER_THRESHOLD (box opened, light floods in)
 */

class LDRModule {
public:
    LDRModule(uint8_t pin = 35);
    
    void begin();
    
    // Read raw ADC value (0-4095)
    uint16_t readRaw();
    
    // Read smoothed value (moving average)
    uint16_t readSmoothed();
    
    // Check if tamper detected
    bool isTamper();
    
    // Get current light level
    uint16_t getLightLevel() { return current_light; }
    
    // Get tamper state
    bool getTamperState() { return tamper_detected; }
    
    // Set tamper threshold manually
    void setTamperThreshold(uint16_t threshold) { TAMPER_THRESHOLD = threshold; }
    
    // Reset tamper flag (after acknowledgement)
    void resetTamper() { tamper_detected = false; }
    
private:
    uint8_t adc_pin;
    uint16_t current_light;
    bool tamper_detected;
    
    // Tamper threshold: if light > this value -> box opened
    uint16_t TAMPER_THRESHOLD = 500;  // Adjust based on calibration
    
    // Moving average for noise filtering
    static const uint8_t FILTER_SIZE = 8;
    uint16_t filter_buffer[FILTER_SIZE];
    uint8_t filter_idx;
    
    uint16_t calculateAverage();
};
