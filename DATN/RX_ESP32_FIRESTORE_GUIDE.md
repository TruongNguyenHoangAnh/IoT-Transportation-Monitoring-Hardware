# RX ESP32 + Firestore Integration Guide

## 📋 Tóm tắt hệ thống hiện tại

### ✅ Đã hoàn thành:

1. **LoRa TX Modules (Node 218, Node 201):**
   - Gửi mock sensor data mỗi 10 giây
   - Anti-replay protection (sequence tracking)
   - CRC16 integrity check
   - JSON format: `{"vehicle_id":"NODE-218","temp":33.8,...}`

2. **LoRa RX Gateway (ASR6601):**
   - Nhận gói tin từ TX via LoRa radio
   - Validate sequence + CRC + MAX_JUMP
   - In payload ra serial port
   - Per-node statistics (accepted/rejected)

3. **ESP32 (Node 218 - TX side):**
   - Auto-detect node_id từ LoRa hoặc sinh từ Chip ID
   - Đọc cảm biến thực (DHT11, ADXL345, LDR, GPS)
   - Gửi JSON via UART tới LoRa TX

### ⏳ Cần làm:

**Folder mới: RX_ESP32_Firestore/**

1. **RX ESP32 (kết nối với ASR6601 RX gateway):**
   - Đọc payload từ RX serial port (UART)
   - Parse JSON payload
   - Format dữ liệu
   - Lưu vào Firestore

2. **Firestore Integration:**
   - Kết nối Firestore
   - Tạo collection: `vehicles` → `node_telemetry`
   - Upload real-time data
   - Query stats dashboard

---

## 🏗️ Kiến trúc kết nối

```
┌─────────────────────────────────────────────────────────────┐
│                    CONVOY VEHICLES                          │
├─────────────────────────────────────────────────────────────┤
│ Node 218 (ESP32)           │ Node 201 (ESP32)              │
│  ↓                         │  ↓                            │
│ JSON via UART1 (GPIO26)    │ JSON via UART1               │
│  ↓                         │  ↓                           │
│ LoRa TX Module (ASR6601)   │ LoRa TX Module (ASR6601)     │
│  ↓                         │  ↓                           │
└─────────────────────────────────────────────────────────────┘
                              ↓ LoRa Radio
┌─────────────────────────────────────────────────────────────┐
│                    COMMAND CENTER (GATEWAY)                  │
├─────────────────────────────────────────────────────────────┤
│ LoRa RX Module (ASR6601)                                    │
│  ↓                                                           │
│ RX Serial Output: [RX OK] node=218, len=145                 │
│                   Payload: {...JSON...}                     │
│  ↓                                                           │
│ ESP32 (NEW) - Folder: RX_ESP32_Firestore                    │
│  ├─ Read serial ← RX gateway payload                        │
│  ├─ Parse JSON                                              │
│  ├─ Validate + Transform                                    │
│  └─ Upload → Firestore Cloud                                │
│      ↓                                                       │
│   Firestore Database                                        │
│      ├─ vehicles/ (collection)                              │
│      │  ├─ NODE-218/ (doc)                                  │
│      │  │  └─ telemetry/ (subcollection)                    │
│      │  │     ├─ timestamp_1: {temp, hum, accel, ...}       │
│      │  │     └─ timestamp_2: {temp, hum, accel, ...}       │
│      │  └─ NODE-201/ (doc)                                  │
│      │     └─ telemetry/ (subcollection)                    │
│      └─ statistics/ (collection)                            │
│         ├─ NODE-218: {last_seen, packet_count, ...}         │
│         └─ NODE-201: {last_seen, packet_count, ...}         │
│                                                              │
│   Dashboard / Mobile App (reads from Firestore)             │
└─────────────────────────────────────────────────────────────┘
```

---

## 📁 Folder cấu trúc RX_ESP32_Firestore/

```
RX_ESP32_Firestore/
├── platformio.ini                    # PlatformIO config
├── README.md                         # Hướng dẫn setup
├── firestore_secrets.h               # Firebase credentials (GIT IGNORE)
├── src/
│   ├── main.cpp                      # Entry point
│   ├── serial_reader.h               # Đọc RX serial
│   ├── serial_reader.cpp             
│   ├── json_parser.h                 # Parse JSON từ RX
│   ├── json_parser.cpp               
│   ├── firestore_client.h            # Kết nối Firestore
│   ├── firestore_client.cpp          
│   └── config.h                      # Constants + pin definitions
├── include/
│   └── README
└── lib/
    └── README
```

---

## 📝 File chi tiết cần tạo

### 1. **platformio.ini**
```ini
[env:nodemcu-32s]
platform = espressif32
board = nodemcu-32s
framework = arduino

lib_deps =
    bblanchon/ArduinoJson@^6.19.4      # JSON parsing
    mobizt/Firebase ESP32 Client@latest # Firestore
    
monitor_speed = 115200
upload_speed = 921600
```

### 2. **config.h**
```cpp
// Serial: RX Gateway đưa dữ liệu qua UART0
#define SERIAL_BAUD 115200
#define SERIAL_RX_PIN 3
#define SERIAL_TX_PIN 1

// WiFi
#define WIFI_SSID "your_ssid"
#define WIFI_PASS "your_password"

// Firestore
#define FIREBASE_PROJECT_ID "your-project-id"
#define FIREBASE_API_KEY "your-api-key"
#define FIREBASE_EMAIL "your-email@gmail.com"
#define FIREBASE_PASSWORD "your-password"
```

### 3. **main.cpp** - Flow chính
```
Loop:
  1. Đọc line từ RX serial (RX gateway output)
     Format: [RX OK] node=218, seq=X, len=Y, rssi=-Z, snr=W
             Payload: {...JSON...}
  
  2. Extract node_id + payload từ line
  
  3. Parse JSON:
     - vehicle_id
     - temp
     - humidity
     - accel_mag
     - gps (lat, lng)
     - light_level
     - tamper
     - status
  
  4. Thêm metadata:
     - timestamp (millis/unix)
     - rssi (signal strength)
     - snr (signal-to-noise)
     - received_at (gateway time)
  
  5. Upload → Firestore:
     /vehicles/{vehicle_id}/telemetry/{timestamp} = {...payload...}
     /statistics/{vehicle_id} = {last_seen, packet_count, rssi_avg, ...}
  
  6. Kiểm tra WiFi + reconnect nếu cần
```

### 4. **serial_reader.cpp** - Đọc RX gateway
```cpp
// Read line from RX serial: [RX OK] node=218, seq=1, ...
// Return: struct RXPacket {node_id, seq, len, rssi, snr, payload}

bool readRXLine(String& line) {
    if (Serial.available()) {
        line = Serial.readStringUntil('\n');
        return true;
    }
    return false;
}

struct RXPacket parseRXLine(String line) {
    // Parse: [RX OK] node=218, seq=1, len=145, rssi=-27, snr=12
    //        Payload: {"vehicle_id":"NODE-218",...}
    
    RXPacket pkt;
    // Extract node, seq, len, rssi, snr
    // Extract payload (từ "Payload: {" đến "}")
    return pkt;
}
```

### 5. **json_parser.cpp** - Parse JSON từ RX
```cpp
// Input: Payload string từ RX
// Output: struct TelemetryData {vehicle_id, temp, hum, accel_mag, gps, light, tamper, status}

struct TelemetryData parsePayload(String payloadStr) {
    // Dùng ArduinoJson để parse:
    // {
    //   "vehicle_id": "NODE-218",
    //   "temp": 33.8,
    //   "hum": 56.7,
    //   "accel_mag": 1.41,
    //   "gps": {"lat": 21.0295, "lng": 105.8581},
    //   "light_level": 211,
    //   "tamper": 0,
    //   "status": "OK"
    // }
    
    TelemetryData data;
    // Parse fields
    return data;
}
```

### 6. **firestore_client.cpp** - Kết nối Firestore
```cpp
// Upload telemetry data lên Firestore

void uploadTelemetry(String vehicleId, TelemetryData data, uint32_t timestamp) {
    // Add metadata
    data.timestamp = timestamp;
    data.received_at = now();
    
    // Insert to: /vehicles/{vehicleId}/telemetry/{timestamp}
    // Firestore.updateDocument(
    //     "/vehicles/" + vehicleId + "/telemetry/" + timestamp,
    //     data
    // );
    
    // Update statistics: /statistics/{vehicleId}
    // {last_seen: now, packet_count: ++count, rssi_avg: ..., ...}
}

void updateStatistics(String vehicleId, RXPacket packet) {
    // /statistics/{vehicleId}
    // Track: last_seen, total_packets, rssi_average, snr_average, ...
}
```

---

## 🔑 Firestore Collection Schema

### Collection: `/vehicles/{vehicle_id}/telemetry/{timestamp}`
```json
{
  "vehicle_id": "NODE-218",
  "timestamp": 1234567890000,
  "received_at": "2026-03-08T20:45:31Z",
  "temp": 33.8,
  "humidity": 56.7,
  "accel_mag": 1.41,
  "gps": {
    "lat": 21.0295,
    "lng": 105.8581
  },
  "light_level": 211,
  "tamper": 0,
  "status": "OK",
  "gateway": {
    "rssi": -27,
    "snr": 12,
    "seq": 145
  }
}
```

### Collection: `/statistics/{vehicle_id}`
```json
{
  "vehicle_id": "NODE-218",
  "last_seen": 1234567890000,
  "total_packets": 42,
  "packets_today": 28,
  "rssi_average": -28.5,
  "snr_average": 11.8,
  "tamper_events": 3,
  "last_status": "OK",
  "battery_voltage": 4.0,
  "temperature_avg": 30.2,
  "updated_at": "2026-03-08T20:45:31Z"
}
```

---

## 📡 UART Protocol (RX Gateway → RX ESP32)

**Input từ RX gateway serial:**
```
[RX OK] node=218, seq=3, len=145, rssi=-27, snr=12
        Payload: {"vehicle_id":"NODE-218","temp":33.8,"hum":56.7,"accel_mag":1.41,"gps":{"lat":21.0295,"lng":105.8581},"light_level":211,"tamper":0,"status":"OK"}

[RX OK] node=201, seq=6, len=9, rssi=-40, snr=12
        Payload: HEARTBEAT
```

**Pattern để parse:**
```
^\[RX OK\] node=(\d+), seq=(\d+), len=(\d+), rssi=(-?\d+), snr=(\d+)
\s+Payload: (.+)$
```

---

## 🚀 Implementation Steps (cho AI bên folder mới)

1. **Setup WiFi + Firebase credentials**
   - Kết nối WiFi
   - Initialize Firebase/Firestore
   - Auto-reconnect nếu mất signal

2. **Main loop:**
   - Đọc serial từ RX gateway
   - Parse RX packet (node_id, seq, rssi, snr, payload)
   - Parse JSON payload
   - Enrich data (thêm timestamp, gateway info)
   - Upload → Firestore

3. **Error handling:**
   - Serial read timeout
   - JSON parse error
   - Firestore upload failure → queue & retry
   - WiFi disconnect → reconnect + sync pending

4. **Optimization:**
   - Buffer pending uploads nếu WiFi down
   - Batch uploads (mỗi minute, không upload liên tục)
   - Compress data nếu cần

5. **Monitoring:**
   - Print stats: uploaded packets, failed packets, memory usage
   - Firestore quota monitoring

---

## 📚 Dependencies

```
- ArduinoJson (JSON parsing)
- Firebase ESP32 Client (Firestore)
- WiFi (built-in)
```

---

## 🔐 Security Notes

- **firestore_secrets.h**: Không commit lên Git (.gitignore)
- Dùng Environment Variables hoặc EEPROM cho credentials
- Validate all inputs trước upload
- Rate limiting trên Firestore

---

## ✅ Testing Checklist

- [ ] ESP32 kết nối WiFi
- [ ] Firebase credentials đúng
- [ ] Đọc serial từ RX thành công
- [ ] Parse JSON chính xác
- [ ] Upload đến Firestore (check console)
- [ ] Firestore documents created correctly
- [ ] WiFi reconnect works
- [ ] Handle malformed JSON gracefully
- [ ] Check Firestore quota / billing

---

## 📞 Ghi chú

- RX gateway sẽ print ra serial theo format cố định - không cần thay đổi RX code
- ESP32 RX chỉ cần đọc và forward data lên Firestore
- Có thể thêm local storage (SD card) để backup data khi WiFi down
- Firestore timestamps: dùng server timestamp cho consistency across devices
