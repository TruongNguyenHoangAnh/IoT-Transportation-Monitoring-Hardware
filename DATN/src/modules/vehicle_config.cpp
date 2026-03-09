#include "vehicle_config.h"

// Global instance
VehicleConfig gVehicleConfig;

VehicleConfig::VehicleConfig() : vehicle_num(1) {
    memset(device_id, 0, sizeof(device_id));
    // Default device ID based on compile-time macro if available
    #ifdef VEHICLE_DEVICE_ID
        strncpy(device_id, VEHICLE_DEVICE_ID, sizeof(device_id) - 1);
    #else
        strncpy(device_id, "VX", sizeof(device_id) - 1);
    #endif
}

void VehicleConfig::begin() {
    EEPROM.begin(EEPROM_SIZE);
    delay(10);
    
    // Check if EEPROM has been initialized
    uint8_t magic = EEPROM.read(ADDR_MAGIC);
    if (magic == MAGIC_BYTE) {
        // Load from EEPROM
        loadFromEEPROM();
    } else {
        // First time: save default values
        Serial.println("[VEHICLE] First boot - initializing EEPROM with defaults");
        saveToEEPROM();
    }
    
    Serial.printf("[VEHICLE] Initialized: %s (Vehicle #%d)\r\n", device_id, vehicle_num);
}

const char* VehicleConfig::getDeviceId() {
    return device_id;
}

String VehicleConfig::buildDeviceString() {
    // Return formatted string: "ESP32-AMMO-VX-01"
    String result = "ESP32-AMMO-";
    result += device_id;
    return result;
}

void VehicleConfig::setDeviceId(const char* id) {
    if (id == NULL) return;
    
    strncpy(device_id, id, sizeof(device_id) - 1);
    device_id[sizeof(device_id) - 1] = '\0';
    
    saveToEEPROM();
    Serial.printf("[VEHICLE] Device ID updated to: %s\r\n", device_id);
}

void VehicleConfig::setDeviceIdFromNodeId(uint8_t node_id) {
    // Convert numeric node_id to string format: "NODE-123"
    char buf[32];
    snprintf(buf, sizeof(buf), "NODE-%d", (int)node_id);
    setDeviceId(buf);
    Serial.printf("[VEHICLE] Auto-detected from LoRa: %s\r\n", device_id);
}

uint8_t VehicleConfig::getVehicleNumber() {
    return vehicle_num;
}

void VehicleConfig::setVehicleNumber(uint8_t num) {
    if (num < 1 || num > 99) return;
    
    vehicle_num = num;
    
    // Also update device_id based on number
    char buf[32];
    snprintf(buf, sizeof(buf), "VX-%02d", num);
    setDeviceId(buf);
}

void VehicleConfig::loadFromEEPROM() {
    // Load device_id (32 bytes)
    for (int i = 0; i < 31; i++) {
        device_id[i] = EEPROM.read(ADDR_DEVICE_ID + i);
        if (device_id[i] == 0) break;
    }
    device_id[31] = '\0';
    
    // Load vehicle number
    vehicle_num = EEPROM.read(ADDR_VEHICLE_NUM);
    if (vehicle_num == 0 || vehicle_num > 99) {
        vehicle_num = 1;  // Safety check
    }
}

void VehicleConfig::saveToEEPROM() {
    // Save device_id (32 bytes)
    for (int i = 0; i < 31; i++) {
        EEPROM.write(ADDR_DEVICE_ID + i, device_id[i]);
    }
    EEPROM.write(ADDR_DEVICE_ID + 31, 0);  // Null terminator
    
    // Save vehicle number
    EEPROM.write(ADDR_VEHICLE_NUM, vehicle_num);
    
    // Save magic byte
    EEPROM.write(ADDR_MAGIC, MAGIC_BYTE);
    
    EEPROM.commit();
    delay(10);
}
