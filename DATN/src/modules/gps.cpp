#include "gps.h"
#include <Arduino.h>

GPSNeo6M::GPSNeo6M(int rxPin, int txPin, long baud)
  : _rxPin(rxPin), _txPin(txPin), _baud(baud), _serial(2) {}

void GPSNeo6M::begin() {
  _serial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
  Serial.println("=== GPS NEO-6M Init ===");
  Serial.println("Waiting for GPS signal...");
}

void GPSNeo6M::read() {
  // ⚠️ DEBUG: Set to 1 to see raw NMEA, 0 to disable
  #define GPS_DEBUG_RAW 1
  
  while (_serial.available() > 0) {
    char c = _serial.read();
    _gps.encode(c);
    
    #if GPS_DEBUG_RAW
    // Capture raw NMEA output (debug only)
    static char nmea_buffer[200];
    static uint16_t nmea_idx = 0;
    if (c == '\n') {
      nmea_buffer[nmea_idx] = '\0';
      Serial.printf("[GPS-RAW] %s", nmea_buffer);
      nmea_idx = 0;
    } else if (nmea_idx < sizeof(nmea_buffer) - 1) {
      nmea_buffer[nmea_idx++] = c;
    }
    #endif
  }
}

void GPSNeo6M::printLocation() {
  if (_gps.location.isUpdated()) {
    Serial.print("Latitude : ");  Serial.println(_gps.location.lat(), 6);
    Serial.print("Longitude: ");  Serial.println(_gps.location.lng(), 6);
    Serial.print("Satellites: "); Serial.println(_gps.satellites.value());
    printTimestamp();
    Serial.println("-----------------------");
  }
}

void GPSNeo6M::printTimestamp() {
	if (_gps.time.isValid() && _gps.date.isValid()) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d UTC",
			_gps.date.year(), _gps.date.month(), _gps.date.day(),
			_gps.time.hour(), _gps.time.minute(), _gps.time.second());
		Serial.print("Timestamp : ");
		Serial.println(buf);
	} else {
		Serial.println("Timestamp : (no fix)");
	}
}

bool GPSNeo6M::buildTimestamp(char* buf, size_t n) {
  if (_gps.time.isValid() && _gps.date.isValid()) {
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d",
             _gps.date.year(), _gps.date.month(), _gps.date.day(),
             _gps.time.hour(), _gps.time.minute(), _gps.time.second());
    return true;
  }
  return false;
}

// ⚠️ DEBUG: Print GPS stats to troubleshoot connection issues
void GPSNeo6M::printDebugStats() {
  static uint32_t last_print = 0;
  uint32_t now = millis();
  if (now - last_print < 3000) return;  // Print every 3s
  last_print = now;
  
  uint32_t bytes_available = _serial.available();
  uint32_t chars_processed = _gps.charsProcessed();
  uint32_t sentences_with_fix = _gps.sentencesWithFix();
  bool has_fix = hasFix();
  
  Serial.println("\n╔═══ GPS DEBUG STATS ═══╗");
  Serial.printf("║ Serial Bytes Waiting: %lu\r\n", bytes_available);
  Serial.printf("║ Chars Processed: %lu\r\n", chars_processed);
  Serial.printf("║ Sentences with Fix: %lu\r\n", sentences_with_fix);
  Serial.printf("║ Location Valid: %s\r\n", _gps.location.isValid() ? "YES" : "NO");
  Serial.printf("║ Satellites: %d\r\n", _gps.satellites.value());
  Serial.printf("║ Has Fix: %s\r\n", has_fix ? "YES" : "NO");
  if (has_fix) {
    Serial.printf("║ Lat/Lng: %.6f, %.6f\r\n", _gps.location.lat(), _gps.location.lng());
  }
  Serial.println("╚═══════════════════════╝\n");
}
