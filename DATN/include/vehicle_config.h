#ifndef VEHICLE_CONFIG_H
#define VEHICLE_CONFIG_H
#include <Arduino.h>
#include <EEPROM.h>

/**
 * Vehicle Configuration Module
 * 
 * Cho phép cấu hình unique device_id cho mỗi xe trong đoàn.
 * Device ID có thể được set qua:
 * 1. Compile-time macro (VEHICLE_DEVICE_ID)
 * 2. EEPROM storage
 * 3. Runtime via serial command
 */

class VehicleConfig {
public:
    VehicleConfig();
    
    // Initialize vehicle config (load from EEPROM or use default)
    void begin();
    
    // Get device ID (e.g., "VX-01", "VX-02", ...)
    const char* getDeviceId();
    
    // Get vehicle name (e.g., "AMMO-VX-01")
    String buildDeviceString();
    
    // Set device ID in EEPROM
    void setDeviceId(const char* id);
    
    // Set device ID from node_id (numeric) - converts to format like "Transport-123"
    void setDeviceIdFromNodeId(uint8_t node_id);
    
    // Get vehicle number (1-99)
    uint8_t getVehicleNumber();
    
    // Set vehicle number (stored in EEPROM)
    void setVehicleNumber(uint8_t num);
    
private:
    char device_id[32];
    uint8_t vehicle_num;
    
    // EEPROM layout
    static const uint16_t EEPROM_SIZE = 256;
    static const uint16_t ADDR_DEVICE_ID = 0;      // 32 bytes
    static const uint16_t ADDR_VEHICLE_NUM = 32;   // 1 byte
    static const uint16_t ADDR_MAGIC = 33;         // 1 byte magic
    static const uint8_t MAGIC_BYTE = 0xAA;
    
    void loadFromEEPROM();
    void saveToEEPROM();
};

// Global instance
extern VehicleConfig gVehicleConfig;

#endif // VEHICLE_CONFIG_H
