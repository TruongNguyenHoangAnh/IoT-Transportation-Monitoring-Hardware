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
  while (_serial.available() > 0) {
    _gps.encode(_serial.read());
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
