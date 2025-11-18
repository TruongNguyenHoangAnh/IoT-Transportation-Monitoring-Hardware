#include "lora.h"

void LoRaAT::begin(int rx, int tx, uint32_t baud) {
  _ser.begin(baud, SERIAL_8N1, rx, tx);
  delay(100);
  flushInput();
  atOk("AT"); // wake & check
}

void LoRaAT::flushInput() {
  while (_ser.available()) _ser.read();
}

bool LoRaAT::waitFor(const String &needle, uint32_t timeout_ms, String *rsp) {
  uint32_t t0 = millis();
  String buf;
  while (millis() - t0 < timeout_ms) {
    while (_ser.available()) {
      char c = (char)_ser.read();
      buf += c;
    }
    if (buf.indexOf(needle) >= 0) {
      if (rsp) *rsp = buf;
      return true;
    }
    delay(5);
  }
  if (rsp) *rsp = buf;
  return false;
}

bool LoRaAT::atOk(const String &cmd, String *rsp, uint32_t timeout_ms) {
  flushInput();
  _ser.print(cmd); _ser.print("\r\n");
  return waitFor("OK", timeout_ms, rsp);
}

bool LoRaAT::atQuery(const String &cmd, String &resp, uint32_t timeout_ms) {
  flushInput();
  _ser.print(cmd); _ser.print("\r\n");
  return waitFor("OK", timeout_ms, &resp);
}

void LoRaAT::setRegion(const String &region) {
  // Nếu FW không hỗ trợ CREGION thì lệnh vẫn trả lỗi; bỏ qua.
  atOk("AT+CREGION=" + region);
  // Với AS923, nhiều FW dùng band mask. Bạn đã có 0001, nên không ép lại ở đây.
}

void LoRaAT::setKeys(const String &devEui, const String &appEui, const String &appKey) {
  _devEui = devEui; _appEui = appEui; _appKey = appKey;
  atOk("AT+CDEVEUI=" + _devEui);
  atOk("AT+CAPPEUI=" + _appEui);
  atOk("AT+CAPPKEY=" + _appKey);
  atOk("AT+CCLASS=0"); // Class A
}

bool LoRaAT::joinOTAA(uint32_t timeout_ms, uint8_t trials, uint8_t interval_s) {
  // format: AT+CJOIN=<mode>,<auto_tx>,<trials>,<interval_seconds>
  String cmd = "AT+CJOIN=1,0," + String(trials) + "," + String(interval_s);
  String rsp;
  atOk(cmd, &rsp, 800); // gửi lệnh
  // Đợi "JOINED" / "ACCEPTED" xuất hiện
  bool ok = waitFor("JOINED", timeout_ms, &rsp) || waitFor("ACCEPTED", timeout_ms, &rsp);
  return ok;
}

// Chuẩn bị chuỗi HEX không dấu cách
String LoRaAT::stripSpaces(const String &s) {
  String out; out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) if (s[i] != ' ' && s[i] != '\r' && s[i] != '\n') out += s[i];
  return out;
}

bool LoRaAT::sendHex(const String &hex, bool confirmed, uint8_t fport, uint32_t timeout_ms) {
  // Tùy FW: ưu tiên AT+MSGHEX; nếu FW không hỗ trợ, dùng AT+DTRX
  String payload = stripSpaces(hex);
  String rsp;

  // Thử AT+MSGHEX trước
  flushInput();
  _ser.print("AT+MSGHEX="); _ser.print(payload); _ser.print("\r\n");
  if (waitFor("OK", 1500, &rsp)) {
    // Chờ "Done" hoặc echo gửi xong
    return waitFor("Done", timeout_ms, &rsp) || waitFor("TX DONE", timeout_ms, &rsp) || waitFor("Send done", timeout_ms, &rsp);
  }

  // Fallback: AT+DTRX=<type>,<port>,<confirm>,<len> <hex>
  flushInput();
  int len = payload.length() / 2;
  String cmd = "AT+DTRX=1," + String((int)fport) + "," + String(confirmed ? 1 : 0) + "," + String(len) + " " + payload;
  _ser.print(cmd); _ser.print("\r\n");
  return waitFor("OK", 1500, &rsp) && (waitFor("Done", timeout_ms, &rsp) || waitFor("TX DONE", timeout_ms, &rsp));
}

bool LoRaAT::readDownlink(String &outHex, uint8_t &outPort) {
  // Đọc toàn bộ buffer và tìm dấu hiệu RX. Mỗi FW in khác nhau; bắt các pattern phổ biến.
  String buf;
  delay(50);
  while (_ser.available()) buf += (char)_ser.read();
  if (buf.length() == 0) return false;

  // Một số FW in: "RX: PORT:1; RX: 01 02 ..." hoặc "PORT: 1, DATA: 01 02"
  // Ta gom hết hex lại.
  // Port
  int p1 = buf.indexOf("PORT");
  if (p1 >= 0) {
    int colon = buf.indexOf(':', p1);
    if (colon >= 0) {
      outPort = (uint8_t) strtoul(buf.substring(colon + 1).c_str(), nullptr, 10);
    } else {
      outPort = 0;
    }
  } else {
    outPort = 0;
  }

  // Hex
  String hex;
  for (size_t i = 0; i < buf.length(); ++i) {
    char c = buf[i];
    if ((c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f')) {
      hex += (char)toupper(c);
    }
  }
  // Loại bỏ phần không phải payload nếu có (đơn giản hoá: trả nguyên chuỗi hex gom được)
  if (hex.length() >= 2) {
    outHex = hex;
    return true;
  }
  return false;
}
