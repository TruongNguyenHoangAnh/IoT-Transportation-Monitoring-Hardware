
#include "gps.h"
#include <Arduino.h>

GPSNeo6M::GPSNeo6M(int rxPin, int txPin, long baud)
	: _rxPin(rxPin), _txPin(txPin), _baud(baud), _serial(2) {}


void GPSNeo6M::begin() {
	_serial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
	Serial.println("=== GPS NEO-6M Test ===");
	Serial.println("Waiting for GPS signal...");
	// Khởi tạo SD (CS = 5 mặc định, đổi nếu biết)
	if (SD.begin(5)) {
		Serial.println("SD card OK");
		_sdReady = true;
		// Tạo/lưu file log (append)
		_logFile = SD.open("gps_log.csv", FILE_WRITE);
		if (_logFile) {
			if (_logFile.size() == 0) {
				_logFile.println("timestamp,latitude,longitude,satellites");
			}
			_logFile.flush();
		} else {
			Serial.println("SD file open failed");
			_sdReady = false;
		}
	} else {
		Serial.println("SD card FAIL");
		_sdReady = false;
	}
}

void GPSNeo6M::read() {
	while (_serial.available() > 0) {
		_gps.encode(_serial.read());
	}
}

void GPSNeo6M::printLocation() {
	if (_gps.location.isUpdated()) {
		Serial.print("Latitude : ");
		Serial.println(_gps.location.lat(), 6);
		Serial.print("Longitude: ");
		Serial.println(_gps.location.lng(), 6);
		Serial.print("Satellites: ");
		Serial.println(_gps.satellites.value());
		printTimestamp();
		logToSD();
		Serial.println("-----------------------");
	}
}

void GPSNeo6M::logToSD() {
	if (!_sdReady || !_logFile) return;
	if (_gps.time.isValid() && _gps.date.isValid() && _gps.location.isValid()) {
		char buf[32];
		snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
			_gps.date.year(), _gps.date.month(), _gps.date.day(),
			_gps.time.hour(), _gps.time.minute(), _gps.time.second());
		_logFile.print(buf); _logFile.print(",");
		_logFile.print(_gps.location.lat(), 6); _logFile.print(",");
		_logFile.print(_gps.location.lng(), 6); _logFile.print(",");
		_logFile.println(_gps.satellites.value());
		_logFile.flush();
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