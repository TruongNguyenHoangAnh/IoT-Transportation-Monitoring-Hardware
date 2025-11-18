#pragma once
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>

class GPSNeo6M {
public:
  GPSNeo6M(int rxPin, int txPin, long baud);

  void begin();
  void read();

  // Debug
  void printLocation();
  void printTimestamp();

  // Getter KHÔNG const (TinyGPS++ methods are non-const)
  inline bool     updated()     { return _gps.location.isUpdated(); }
  inline bool     hasFix()      { return _gps.location.isValid() && _gps.satellites.value() > 0; }
  inline double   latitude()    { return _gps.location.lat(); }
  inline double   longitude()   { return _gps.location.lng(); }
  inline uint32_t satellites()  { return _gps.satellites.isValid() ? (uint32_t)_gps.satellites.value() : 0; }

  // Trả chuỗi thời gian GPS "YYYY-MM-DD HH:MM:SS" (return true nếu hợp lệ)
  bool buildTimestamp(char* buf, size_t n);

private:
  int _rxPin, _txPin;
  long _baud;
  HardwareSerial _serial;
  TinyGPSPlus _gps;
};
