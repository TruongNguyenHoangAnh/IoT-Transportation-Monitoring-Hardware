#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>

// Wrapper đơn giản cho LoRa AT (ASR6601/RA-08H)
class LoRaAT {
public:
  explicit LoRaAT(HardwareSerial &ser) : _ser(ser) {}

  void begin(int rx, int tx, uint32_t baud = 9600);
  void setRegion(const String &region);                          // "AS923", "EU868", ...
  void setKeys(const String &devEui, const String &appEui, const String &appKey);
  bool joinOTAA(uint32_t timeout_ms = 20000, uint8_t trials = 8, uint8_t interval_s = 8);

  // Gửi payload HEX (ví dụ "01020304"), confirmed=false => unconfirmed uplink
  bool sendHex(const String &hex, bool confirmed = false, uint8_t fport = 1, uint32_t timeout_ms = 8000);

  // Đọc downlink sau uplink (Class A). Trả true nếu có; outHex không có khoảng trắng.
  bool readDownlink(String &outHex, uint8_t &outPort);

  // Tiện ích
  void flushInput();
  bool atOk(const String &cmd, String *rsp = nullptr, uint32_t timeout_ms = 1500);
  bool atQuery(const String &cmd, String &resp, uint32_t timeout_ms = 1500);

private:
  HardwareSerial &_ser;
  String _devEui, _appEui, _appKey;
  bool waitFor(const String &needle, uint32_t timeout_ms, String *rsp = nullptr);
  static String stripSpaces(const String &s);
};
