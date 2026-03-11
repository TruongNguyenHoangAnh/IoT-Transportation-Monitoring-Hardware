#include "ldr.h"

LDRModule::LDRModule(uint8_t pin) 
    : adc_pin(pin), current_light(0), tamper_detected(false), filter_idx(0) {
    memset(filter_buffer, 0, sizeof(filter_buffer));
}

void LDRModule::begin() {
    // Configure ADC pin - no need for pinMode in ESP32, ADC auto-configures
    analogSetWidth(12);  // 12-bit ADC (0-4095)
    // Note: ADC attenuation auto-configured by Arduino core for GPIO 35
    
    // Initialize filter buffer with current readings to avoid stale data
    for (int i = 0; i < FILTER_SIZE; i++) {
        filter_buffer[i] = analogRead(adc_pin);
        delay(10);  // Small delay between reads
    }
    filter_idx = 0;
}

uint16_t LDRModule::readRaw() {
    uint16_t raw = analogRead(adc_pin);
    // ⚠️ INVERT: Sensor wired inversely (light increases -> ADC decreases)
    // Solution: Subtract from max ADC value (4095)
    uint16_t inverted = 4095 - raw;
    // Normalize from 12-bit ADC (0-4095) to 10-bit (0-1023) for compatibility
    uint16_t normalized = (inverted * 1023) / 4095;
    return normalized;
}

uint16_t LDRModule::readSmoothed() {
    // Add raw reading to filter buffer
    uint16_t raw_reading = readRaw();
    filter_buffer[filter_idx] = raw_reading;
    filter_idx = (filter_idx + 1) % FILTER_SIZE;
    
    // Calculate moving average
    current_light = calculateAverage();
    
    // Check for tamper condition (respond to CURRENT state, not latched)
    // Tamper = true if light level currently exceeds threshold (box is open)
    // Tamper = false if light level below threshold (box is closed)
    bool previous_state = tamper_detected;
    tamper_detected = (current_light > TAMPER_THRESHOLD);
    
    // Optional: Log state transitions only
    if (tamper_detected && !previous_state) {
        Serial.printf("[LDR] TAMPER DETECTED - Light level %u exceeds threshold %u\r\n", 
                     current_light, TAMPER_THRESHOLD);
    } else if (!tamper_detected && previous_state) {
        Serial.printf("[LDR] Tamper cleared - Light level returned to normal (%u)\r\n", 
                     current_light);
    }
    
    return current_light;
}

bool LDRModule::isTamper() {
    readSmoothed();
    return tamper_detected;
}

uint16_t LDRModule::calculateAverage() {
    uint32_t sum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
        sum += filter_buffer[i];
    }
    return (uint16_t)(sum / FILTER_SIZE);
}
