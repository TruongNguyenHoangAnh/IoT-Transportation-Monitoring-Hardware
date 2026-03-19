# RX ESP32 + Firestore Integration Guide

## 📋 Tóm tắt hệ thống hiện tại

### ✅ Đã hoàn thành:

1. **LoRa TX Modules (ESP32 Nodes):**
   - Gửi real sensor data (DHT11, ADXL345, GPS, LDR)
   - Anti-replay protection (sequence tracking)
   - CRC16 integrity check
   - JSON format: `{"vehicle_id":"Transport-1","temp":2.00,"humidity":12.70,...}`
   - Auto-assign node_id (Transport-1, Transport-2, etc.) từ EEPROM counter

2. **LoRa RX Gateway (ASR6601):**
   - Nhận gói tin từ TX via LoRa radio
   - Validate sequence + CRC + MAX_JUMP
   - In payload ra serial port
   - Per-node statistics (accepted/rejected)

3. **ESP32 (Node 218 - TX side):**
   - Auto-detect node_id từ LoRa hoặc sinh từ Chip ID
   - Đọc cảm biến thực (DHT11, ADXL345, LDR, GPS)
   - Gửi JSON via UART tới LoRa TX

### 🔧 Những sửa đổi đã áp dụng (Important for RX ESP32):

1. **Node ID Format:**
   - ❌ OLD: NODE-218, NODE-201 (arbitrary names)
   - ✅ NEW: Transport-1, Transport-2, Transport-3... (auto-increment per device)
   - Mỗi ESP32 boot lần đầu: đọc EEPROM counter → assign node_id → increment counter
   - Device 1 được Transport-1, Device 2 được Transport-2, etc.

2. **Packet Format (Binary Over LoRa):**
   ```
   [node_id (1 byte)] [seq (4 bytes LE)] [len (2 bytes LE)] [JSON payload (var)] [CRC16 (2 bytes LE)]
   
   Ví dụ binary:
   - Byte 0: node_id = 0x01 (Transport-1)
   - Bytes 1-4: seq = 0x00000000 (little-endian)
   - Bytes 5-6: len = 0x92 0x00 = 146 bytes (little-endian)
   - Bytes 7-152: JSON payload
   - Bytes 153-154: CRC16 checksum
   ```

3. **JSON Payload Format (from TX ESP32):**
   ```json
   {
     "vehicle_id": "Transport-1",
     "timestamp": 22964,
     "seq": 10,
     "temp": 2.00,
     "humidity": 12.70,
     "accel_mag": 0.98,
     "gps": {
       "lat": 0.0000,
       "lng": 0.0000,
       "altitude": 0.0
     },
     "light_level": 512,
     "tamper": 0,
     "status": "OK"
   }
   ```

4. **RX Gateway Serial Output Format:**
   ```
   [RX OK] node=1, seq=0, len=162, rssi=-95, snr=7
   Payload: {"vehicle_id":"Transport-1","timestamp":22964,"seq":10,"temp":2.00,...}
   ```
   - **node**: 1-byte integer (0-255)
   - **seq**: sequence number (anti-replay)
   - **len**: payload length in bytes
   - **rssi**: Received Signal Strength Indicator (-120 to 0)
   - **snr**: Signal-to-Noise Ratio
   - **vehicle_id**: Now "Transport-X" format (X = 1, 2, 3, ...)

5. **Critical RX Parser Fix:**
   - ❌ OLD: Parse node_id as 4-byte integer (causing misalignment)
   - ✅ NEW: Parse node_id as 1-byte uint8_t
   - Minimum packet size: 9 bytes (1+4+2+0+2) instead of 12 bytes

6. **Removed TX Broadcasts:**
   - ❌ OLD: TX module sent status messages like "[LORA-NODE-ID]", "VEHICLE_READY"
   - ✅ NEW: Only sensor JSON payloads sent (clean protocol)
   - Reduces noise and CRC failures

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
│ Transport-1 (ESP32)        │ Transport-2 (ESP32)           │
│  ↓                         │  ↓                            │
│ Real Sensor Data (JSON)    │ Real Sensor Data (JSON)       │
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
│ RX Serial Output: [RX OK] node=1, len=162, rssi=-95        │
│                   Payload: {...JSON...}                     │
│  ↓                                                           │
│ ESP32 (RX) - Folder: RX_ESP32_Firestore                    │
│  ├─ Read serial ← RX gateway payload                        │
│  ├─ Parse JSON                                              │
│  ├─ Validate + Transform                                    │
│  └─ Upload → Firestore Cloud                                │
│      ↓                                                       │
│   Firestore Database                                        │
│      ├─ vehicles/ (collection)                              │
│      │  ├─ Transport-1/ (doc)                               │
│      │  │  └─ telemetry/ (subcollection)                    │
│      │  │     ├─ timestamp_1: {temp, hum, accel, ...}       │
│      │  │     └─ timestamp_2: {temp, hum, accel, ...}       │
│      │  └─ Transport-2/ (doc)                               │
│      │     └─ telemetry/ (subcollection)                    │
│      └─ statistics/ (collection)                            │
│         ├─ Transport-1: {last_seen, packet_count, ...}      │
│         └─ Transport-2: {last_seen, packet_count, ...}      │
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

⚠️ **IMPORTANT FIX:** The RX gateway sends TWO lines per packet:
- Line 1: `[RX OK] node=X, seq=Y, len=Z, rssi=A, snr=B`
- Line 2: `Payload: {...JSON...}`

You MUST read both lines and combine them!

```cpp
// Read TWO lines from RX serial and combine
// Line 1: [RX OK] node=1, seq=0, len=162, rssi=-95, snr=7
// Line 2: Payload: {"vehicle_id":"Transport-1",...}

struct RXPacket {
    uint8_t node_id;      // 1-byte, range 1-255
    uint32_t seq;         // 4-byte sequence
    uint16_t len;         // 2-byte payload length
    int16_t rssi;         // Signal strength (-120 to 0)
    int8_t snr;           // Signal-to-noise ratio
    String payload;       // JSON string
};

bool readRXPacket(RXPacket& pkt) {
    String line1, line2;
    
    // Read first line: [RX OK] node=X, seq=Y, ...
    if (!Serial.available()) return false;
    
    line1 = Serial.readStringUntil('\n');
    if (!line1.startsWith("[RX OK]")) return false;
    
    // Parse: [RX OK] node=1, seq=0, len=162, rssi=-95, snr=7
    sscanf(line1.c_str(), "[RX OK] node=%hhu, seq=%lu, len=%hu, rssi=%hd, snr=%hhd",
           &pkt.node_id, &pkt.seq, &pkt.len, &pkt.rssi, &pkt.snr);
    
    // Read second line: Payload: {...}
    line2 = Serial.readStringUntil('\n');
    if (!line2.startsWith("Payload:")) return false;
    
    // Extract JSON part (from '{' to '}')
    int start = line2.indexOf('{');
    if (start == -1) return false;
    pkt.payload = line2.substring(start);
    
    return true;
}

struct RXPacket parseRXLine(String line) {
    // OLD APPROACH (don't use this):
    // Parse: [RX OK] node=218, seq=1, len=145, rssi=-27, snr=12
    //        Payload: {"vehicle_id":"NODE-218",...}
    
    // ✅ NEW APPROACH: readRXPacket() above handles 2-line protocol
    
    RXPacket pkt;
    // Extract node, seq, len, rssi, snr
    // Extract payload (từ "Payload: {" đến "}")
    return pkt;
}
```

### 5. **json_parser.cpp** - Parse JSON từ RX

⚠️ **UPDATED:** vehicle_id is now "Transport-X" format!

```cpp
// Input: Payload string từ RX
// Output: struct TelemetryData {vehicle_id, temp, hum, accel_mag, gps, light, tamper, status}

struct GPSData {
    float lat;
    float lng;
    float altitude;
};

struct TelemetryData {
    String vehicle_id;      // "Transport-1", "Transport-2", etc.
    uint32_t timestamp;     // milliseconds from ESP32
    uint32_t seq;           // sequence number
    float temp;
    float humidity;
    float accel_mag;
    GPSData gps;
    uint16_t light_level;
    uint8_t tamper;
    String status;
};

bool parsePayload(const String& payloadStr, TelemetryData& data) {
    // Ví dụ payload:
    // {"vehicle_id":"Transport-1","timestamp":22964,"seq":10,"temp":2.00,"humidity":12.70,"accel_mag":0.98,"gps":{"lat":0.0000,"lng":0.0000,"altitude":0.0},"light_level":512,"tamper":0,"status":"OK"}
    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payloadStr);
    
    if (error) {
        Serial.print("[JSON ERROR] ");
        Serial.println(error.f_str());
        return false;
    }
    
    // String fields
    data.vehicle_id = doc["vehicle_id"].as<String>();  // "Transport-1"
    data.status = doc["status"].as<String>();
    
    // Numeric fields
    data.timestamp = doc["timestamp"].as<uint32_t>();
    data.seq = doc["seq"].as<uint32_t>();
    data.temp = doc["temp"].as<float>();
    data.humidity = doc["humidity"].as<float>();
    data.accel_mag = doc["accel_mag"].as<float>();
    data.light_level = doc["light_level"].as<uint16_t>();
    data.tamper = doc["tamper"].as<uint8_t>();
    
    // GPS object
    JsonObject gpsObj = doc["gps"].as<JsonObject>();
    data.gps.lat = gpsObj["lat"].as<float>();
    data.gps.lng = gpsObj["lng"].as<float>();
    data.gps.altitude = gpsObj["altitude"].as<float>();
    
    return true;
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

### ✅ UPDATED: Vehicle ID Format
- ❌ OLD: `/vehicles/NODE-218/telemetry/{timestamp}`
- ✅ NEW: `/vehicles/Transport-1/telemetry/{timestamp}`

The vehicle_id from JSON payload now uses "Transport-X" format automatically!

### Collection: `/vehicles/{vehicle_id}/telemetry/{timestamp}`
```json
{
  "vehicle_id": "Transport-1",
  "timestamp": 22964,
  "seq": 10,
  "received_at": "2026-03-10T14:45:31Z",
  "temp": 2.00,
  "humidity": 12.70,
  "accel_mag": 0.98,
  "gps": {
    "lat": 0.0000,
    "lng": 0.0000,
    "altitude": 0.0
  },
  "light_level": 512,
  "tamper": 0,
  "status": "OK",
  "gateway": {
    "rssi": -95,
    "snr": 7,
    "seq": 0
  }
}
```

### Collection: `/statistics/{vehicle_id}`
```json
{
  "vehicle_id": "Transport-1",
  "last_seen": 1234567890000,
  "total_packets": 42,
  "packets_today": 28,
  "rssi_average": -95.2,
  "snr_average": 7.5,
  "tamper_events": 0,
  "last_status": "OK",
  "temperature_avg": 2.00,
  "updated_at": "2026-03-10T14:45:31Z"
}
```

---

## 📡 UART Protocol (RX Gateway → RX ESP32)

⚠️ **ACTUAL FORMAT (Fixed from document):** RX gateway outputs TWO lines per packet

**Input từ RX gateway serial:**
```
[RX OK] node=1, seq=0, len=162, rssi=-95, snr=7
Payload: {"vehicle_id":"Transport-1","timestamp":22964,"seq":10,"temp":2.00,"humidity":12.70,"accel_mag":0.98,"gps":{"lat":0.0000,"lng":0.0000,"altitude":0.0},"light_level":512,"tamper":0,"status":"OK"}

[RX OK] node=2, seq=1, len=155, rssi=-87, snr=8
Payload: {"vehicle_id":"Transport-2","timestamp":45821,"seq":5,"temp":25.50,"humidity":65.30,"accel_mag":1.02,"gps":{"lat":0.0000,"lng":0.0000,"altitude":0.0},"light_level":450,"tamper":0,"status":"OK"}
```

**Regex pattern untuk parse Line 1:**
```
^\[RX OK\] node=(\d+), seq=(\d+), len=(\d+), rssi=(-?\d+), snr=(\d+)$
```

**Key Points:**
1. node = 1-byte integer (now 1, 2, 3... NOT 218, 201)
2. Each packet takes 2 lines (must read both!)
3. Payload line always starts with "Payload: "
4. JSON starts with '{' and may contain nested objects (gps)
5. No length field in JSON line - read until end of line

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
- [ ] Đọc serial từ RX thành công (both 2 lines)
- [ ] Parse JSON chính xác với vehicle_id format "Transport-X"
- [ ] Extract GPS, temp, humidity, accel_mag từ JSON
- [ ] Upload đến Firestore (check console)
- [ ] Firestore documents created with correct collection path `/vehicles/Transport-1/telemetry/{timestamp}`
- [ ] WiFi reconnect works
- [ ] Handle malformed JSON gracefully (missing fields)
- [ ] Check Firestore quota / billing
- [ ] Verify node_id is 1-byte (0-255 range)
- [ ] Extract rssi, snr from first line correctly

---

## 🧪 Real-World Testing Output

### ✅ Expected Serial Monitor Output (from RX ESP32):
```
[SETUP] Starting RX ESP32 Firestore gateway...
[WIFI] Connecting to MyNetwork...
[WIFI] Connected! IP: 192.168.1.42
[FIREBASE] Connecting to Firebase...
[FIREBASE] Connected to project: transport-iot-xyz

[RX LOOP] Waiting for serial data from RX gateway...

[RX PACKET] Received:
  node_id: 1
  seq: 0
  len: 162
  rssi: -95
  snr: 7

[JSON PARSE] vehicle_id="Transport-1", temp=2.00, humid=12.70, accel=0.98

[FIRESTORE UPLOAD] /vehicles/Transport-1/telemetry/22964
  ✓ Uploaded successfully
  ✓ Statistics updated: /statistics/Transport-1

---

[RX PACKET] Received:
  node_id: 2
  seq: 1
  len: 155
  rssi: -87
  snr: 8

[JSON PARSE] vehicle_id="Transport-2", temp=25.50, humid=65.30, accel=1.02

[FIRESTORE UPLOAD] /vehicles/Transport-2/telemetry/45821
  ✓ Uploaded successfully
  ✓ Statistics updated: /statistics/Transport-2
```

### ✅ Expected Firestore Structure:
```
project/
├── vehicles/ (collection)
│   ├── Transport-1/ (document)
│   │   └── telemetry/ (subcollection)
│   │       ├── 22964: {vehicle_id:"Transport-1", temp:2.00, ...}
│   │       ├── 22975: {vehicle_id:"Transport-1", temp:2.10, ...}
│   │       └── 22986: {vehicle_id:"Transport-1", temp:2.05, ...}
│   ├── Transport-2/ (document)
│   │   └── telemetry/ (subcollection)
│   │       ├── 45821: {vehicle_id:"Transport-2", temp:25.50, ...}
│   │       └── 45832: {vehicle_id:"Transport-2", temp:25.55, ...}
│   └── Transport-3/ (document)
│       └── telemetry/ (subcollection)
│           └── ...
├── statistics/ (collection)
│   ├── Transport-1: {last_seen:NOW(), total_packets:42, rssi_avg:-95.2, ...}
│   ├── Transport-2: {last_seen:NOW(), total_packets:28, rssi_avg:-87.1, ...}
│   └── Transport-3: {...}
```

---

## 📞 Troubleshooting

### Problem: "Failed to read RX serial - timeout"
- **Solution:** Check hardened_pingpong_rx.c is running on ASR6601 RX module
- Verify UART connection between RX module and ESP32 (same baud rate 115200)
- Check serial output on RX module console

### Problem: "JSON parse error" or "Unknown fields"
- **Solution:** RX data might be incomplete or corrupted
- Add timeout protection when readingPayload line
- Log the raw payloadStr before parsing

### Problem: "Firestore upload failed - offline"
- **Solution:** WiFi dropped or Firebase credentials invalid
- Implement local buffering (queue packets to SPIFFS) when offline
- Retry upload when WiFi reconnects

### Problem: "vehicle_id mismatch - expected Transport-X, got NODE-Y"  
- **Solution:** Old RX code might still be transmitting old format
- Make sure ESP32 TX modules are running the **latest** main.cpp with auto-increment node_id
- All devices must boot with updated code for Transport-X naming

---

## 📞 Ghi chú

- **RX gateway sẽ print ra serial theo format cố định (2 lines)** - không cần thay đổi RX code
- **node_id giờ là 1-byte** (0-255 range), không phải 4-byte chip ID
- **vehicle_id định dạng: "Transport-X"** tự động gán từ EEPROM counter khi boot lần đầu
  - Device 1 → Transport-1
  - Device 2 → Transport-2
  - Device 3 → Transport-3
  - etc. (không cần thay code cho từng device!)
- **ESP32 RX chỉ cần đọc serial từ RX gateway và forward data lên Firestore**
- **Firestore path tự động từ JSON payload:** `/vehicles/{vehicle_id}/telemetry/{timestamp}`
  - vehicle_id = "Transport-1" → `/vehicles/Transport-1/telemetry/{timestamp}`
  - vehicle_id = "Transport-2" → `/vehicles/Transport-2/telemetry/{timestamp}`
- Có thể thêm local storage (SPIFFS/SD card) để backup data khi WiFi down
- Firestore timestamps: dùng server timestamp (`FieldValue.serverTimestamp()`) cho consistency across devices
- Removed all TX status broadcasts → protocol cleaner, fewer CRC failures

---

## 🚀 Summary of Changes Applied

✅ **Node ID System:** Upgraded from arbitrary node names to auto-increment Transport-X  
✅ **Packet Format:** Fixed node_id size from 4-byte to 1-byte in RX parser  
✅ **Serial Protocol:** RX gateway outputs 2-line format (metadata + payload)  
✅ **JSON Payload:** Now includes timestamp, seq, full sensor data  
✅ **Firestore Integration:** Each vehicle gets own collection (`/vehicles/Transport-X/telemetry`)  
✅ **GPS/Sensors:** Real sensor reading (DHT11, ADXL345, GPS, LDR) with proper JSON formatting  
✅ **Multi-Vehicle Support:** Simply flash same code to multiple ESP32s, each gets unique Transport-1, 2, 3...  

---

## 🎯 TASK BREAKDOWN FOR AI IMPLEMENTATION

### **PHASE 1: RX ESP32 + Firestore Integration** ⚙️

#### **Objective:**
Create a new folder `RX_ESP32_Firestore/` that reads LoRa packets from ASR6601 RX gateway, parses JSON payload, and uploads telemetry data to Firebase Firestore in real-time.

#### **Task 1.1: Project Setup**
- **Action:** Create new PlatformIO ESP32 project structure
- **Location:** `RX_ESP32_Firestore/` folder
- **Steps:**
  1. Create `platformio.ini` with dependencies: ArduinoJson, Firebase ESP32 Client
  2. Create `src/` and `include/` directories
  3. Configure board: `nodemcu-32s`, baud rate: `115200`
  4. Add `.gitignore` entry for `firestore_secrets.h`

#### **Task 1.2: Configuration Files**
- **Create:** `src/config.h` with constants:
  - WiFi SSID/Password
  - Firebase project ID, API key, email, password
  - Serial pins (RX=3, TX=1)
  - Firestore collection paths: `/vehicles`, `/statistics`
- **Create:** `firestore_secrets.h` (git-ignored) with Firebase credentials
- **Notes:** All hardcoded values should come from config.h for easy deployment

#### **Task 1.3: Serial Reader Module** 
- **Create:** `src/serial_reader.h` + `src/serial_reader.cpp`
- **Responsibility:** Parse 2-line RX gateway output
- **Input Format:**
  ```
  [RX OK] node=1, seq=0, len=162, rssi=-95, snr=7
  Payload: {"vehicle_id":"Transport-1",...}
  ```
- **Output Structure (RXPacket):**
  ```cpp
  struct RXPacket {
    uint8_t node_id;      // 1-byte integer (0-255)
    uint32_t seq;         // 4-byte sequence number
    uint16_t len;         // Payload length in bytes
    int16_t rssi;         // Signal strength (-120 to 0)
    int8_t snr;           // Signal-to-noise ratio
    String payload;       // JSON string
  };
  ```
- **Key Function:** `bool readRXPacket(RXPacket& pkt)`
  - Read first line: extract `[RX OK] node=X, seq=Y, len=Z, rssi=A, snr=B`
  - Read second line: extract JSON from `Payload: {...}`
  - Validate both lines present before returning true
  - Handle timeout/incomplete data gracefully

#### **Task 1.4: JSON Parser Module**
- **Create:** `src/json_parser.h` + `src/json_parser.cpp`
- **Input:** Payload string from RXPacket
- **Output Structure (TelemetryData):**
  ```cpp
  struct TelemetryData {
    String vehicle_id;        // "Transport-1", "Transport-2", etc.
    uint32_t timestamp;       // From ESP32 millis()
    uint32_t seq;             // Packet sequence
    float temp, humidity;     // From DHT11
    float accel_mag;          // From ADXL345
    struct { float lat, lng, altitude; } gps;  // From GPS
    uint16_t light_level;     // From LDR (0-1023)
    uint8_t tamper;           // 0=closed, 1=open
    String status;            // "OK", "ALARM", etc.
  };
  ```
- **Key Function:** `bool parsePayload(const String& payloadStr, TelemetryData& data)`
  - Use ArduinoJson to deserialize JSON
  - Extract all fields with proper data types
  - Handle missing fields gracefully (optional fields)
  - Return false if JSON is malformed
  - Log parse errors to serial for debugging

#### **Task 1.5: Firestore Client Module**
- **Create:** `src/firestore_client.h` + `src/firestore_client.cpp`
- **Functionality:**
  1. **Initialize Firebase:** Connect to FirebaseConfig with credentials
  2. **Upload Telemetry Data:**
     - Destination: `/vehicles/{vehicle_id}/telemetry/{timestamp}`
     - Include all TelemetryData fields + gateway metadata (rssi, snr)
     - Add server-side timestamp
  3. **Update Statistics:**
     - Destination: `/statistics/{vehicle_id}`
     - Track: `last_seen`, `total_packets`, `packets_today`, `rssi_average`, `snr_average`, `tamper_events`, `temperature_avg`, etc.
  4. **Error Handling:**
     - Implement retry logic for failed uploads
     - Queue packets to SPIFFS if WiFi is down
     - Sync queued data when WiFi reconnects
- **Key Functions:**
  - `bool initializeFirebase()`
  - `bool uploadTelemetry(const String& vehicleId, const TelemetryData& data, int16_t rssi, int8_t snr)`
  - `bool updateStatistics(const String& vehicleId, const RXPacket& packet, const TelemetryData& data)`
  - `bool clearOfflineQueue()` (if WiFi down scenario)

#### **Task 1.6: WiFi Manager**
- **Create:** Simple WiFi reconnection logic
- **Requirement:** Auto-reconnect on WiFi loss (don't block main loop)
- **Implementation:** 
  - Check WiFi status in main loop
  - Attempt reconnect if disconnected
  - Log WiFi events (connected, disconnected, failed)
  - Handle certificate validation for Firebase

#### **Task 1.7: Main Loop** 
- **File:** `src/main.cpp`
- **Sequence:**
  ```
  1. Setup:
     - Initialize Serial (115200)
     - Initialize WiFi + Firebase
     - Print startup message
  
  2. Loop (repeat every 100ms):
     - Check if data available on serial
     - Call readRXPacket() → extract metadata + payload
     - Call parsePayload() → convert JSON to TelemetryData
     - Call uploadTelemetry() → send to Firestore
     - Call updateStatistics() → update vehicle stats
     - Log status: [PACKET OK] or [PARSE ERROR] or [UPLOAD FAILED]
  
  3. Error Handling:
     - JSON parse error → log & skip packet
     - Firestore upload error → queue for retry (if offline)
     - WiFi down → print warning, attempt reconnect
  ```
- **Serial Output Example:**
  ```
  [SETUP] RX_ESP32_Firestore v1.0 starting...
  [WIFI] Connecting to MyNetwork...
  [WIFI] Connected! IP: 192.168.1.42
  [FIREBASE] Auth successful
  
  [RX PACKET] node_id=1, rssi=-95, snr=7
  [JSON PARSE] vehicle_id=Transport-1, temp=2.00
  [FIRESTORE] ✓ Uploaded /vehicles/Transport-1/telemetry/22964
  [STATS] ✓ Updated /statistics/Transport-1
  ```

---

### **PHASE 2: k-NN Anomaly Detection (Network Intrusion Detection)** 🧠

#### **Objective:**
Implement k-Nearest Neighbors machine learning algorithm to detect abnormal network patterns (jamming, spoofing, signal attacks) based on RSSI/SNR anomalies.

#### **Task 2.1: Create Python Backend for k-NN**
- **Location:** New folder `knn_anomaly_detector/` in project root
- **Purpose:** Standalone Python script + later integration with RX ESP32 backend
- **File:** `knn_anomaly_detector.py`

#### **Task 2.2: Data Collection & Training Set**
- **Requirement:** Parse historical RX log output to build training data
- **Format to Parse:**
  ```
  [RX OK] node=1, seq=0, len=162, rssi=-67, snr=12
  [RX OK] node=2, seq=1, len=155, rssi=-54, snr=13
  [RX OK] node=1, seq=1, len=162, rssi=-65, snr=12
  ```
- **Features for k-NN:**
  - RSSI (received signal strength): -120 to 0 dBm
  - SNR (signal-to-noise ratio): 0-20+ dB
  - Packet interval (time between consecutive packets from same node): ~2000ms
  - Packet length (should be consistent): ~150-160 bytes
- **Training Data Strategy:**
  - Collect 50-100 normal packets from each vehicle
  - Calculate feature statistics (mean, std dev)
  - Flag outliers as anomalies (e.g., RSSI > 3σ from mean)

#### **Task 2.3: k-NN Algorithm Implementation**
- **Library:** Use `scikit-learn` (sklearn.neighbors.KNeighborsRegressor) or custom implementation
- **Parameters:**
  - k = 5 (neighbors to consider)
  - distance metric = Euclidean
  - features = [RSSI, SNR]
- **Training:**
  - Input: Normal LoRa packet observations (RSSI/SNR pairs)
  - Output: Classify as normal or anomalous
- **Anomaly Detection Logic:**
  ```
  for each new packet:
    1. Extract RSSI, SNR
    2. Find k=5 nearest neighbors in training set
    3. Calculate distance to nearest neighbor
    4. if distance > threshold → FLAG AS ANOMALY
       else → normal packet
  ```
- **Threshold Tuning:**
  - Set threshold based on historical data variability
  - Example: If median distance = 5, threshold = 2x median = 10

#### **Task 2.4: Anomaly Alert System**
- **Detection Triggers:**
  - **RSSI Anomaly:** Signal strength unusually strong (jamming) or weak (spoofing)
  - **SNR Anomaly:** Noisy signal (interference attack)
  - **Sequence Jump:** Gap in packet sequence (dropped packets)
  - **Timing Anomaly:** Irregular packet intervals
- **Alert Actions:**
  1. Log anomaly to console + file
  2. Store anomaly record in Firestore: `/anomalies/{timestamp}`
  3. Push alert to mobile app (Firebase Cloud Messaging)
  4. Update vehicle status: `status: "UNDER_ATTACK"` in statistics collection
- **Alert Structure:**
  ```json
  {
    "vehicle_id": "Transport-1",
    "timestamp": 1234567890,
    "anomaly_type": "RSSI_SPIKE",
    "detected_value": -45,
    "expected_range": [-75, -55],
    "severity": "HIGH",
    "description": "Possible jamming detected - signal strength unusually strong (-45dBm vs expected -65dBm)"
  }
  ```

#### **Task 2.5: Data Storage for k-NN Training**
- **Firestore Collection:** `/knn_training_data/{vehicle_id}/normal_packets`
- **Structure:**
  ```json
  {
    "timestamp": 1234567890,
    "rssi": -67,
    "snr": 12,
    "source": "normal_operation"
  }
  ```
- **Update Strategy:**
  - Only store "normal" packets (no anomalies)
  - Keep rolling 1000-packet window per vehicle
  - Periodically retrain model with fresh data

#### **Task 2.6: Integration with RX ESP32 Firestore**
- **Path 1 (Phase 2a - Standalone):** Python script runs separately, reads Firestore, outputs alerts
- **Path 2 (Phase 2b - Integrated):** Integrate k-NN into RX ESP32 main loop
  - Option A: Call Python subprocess from ESP32 (not ideal - resource constraints)
  - Option B: Upload anomaly detections from Python backend to Firestore (recommended)
  - Option C: Pre-trained model compressed into ESP32 as binary (advanced)
- **Recommended for Now:** Use Path 1 (standalone) + write alerts to Firestore

#### **Task 2.7: Testing & Validation**
- **Test Cases:**
  1. Normal packets → should classify as non-anomalous
  2. Simulated jamming (RSSI = -40 dBm) → should detect as anomaly
  3. Simulated spoofing (sequence jump) → should detect as anomaly
  4. High SNR noise (SNR > 20) → should detect as anomaly
  5. Regular packet stream → no false positives
- **Metrics to Track:**
  - True Positive Rate (TRP): Anomalies correctly detected
  - False Positive Rate (FPR): Normal packets incorrectly flagged
  - Goal: TRP > 95%, FPR < 5%

---

### **IMPLEMENTATION ORDER (Recommended Sequence)**

#### **Week 1:**
1. ✅ Setup RX_ESP32_Firestore project structure (Task 1.1, 1.2)
2. ✅ Implement serial reader for RX gateway output parsing (Task 1.3)
3. ✅ Implement JSON parser for payload deserialization (Task 1.4)
4. ✅ Test parsing with real RX gateway output

#### **Week 2:**
1. ✅ Implement Firestore client & authentication (Task 1.5)
2. ✅ Implement WiFi manager & reconnection logic (Task 1.6)
3. ✅ Implement main loop with end-to-end integration (Task 1.7)
4. ✅ Test live upload to Firestore with actual RX packets

#### **Week 3:**
1. ✅ Create Python k-NN anomaly detector (Task 2.1-2.3)
2. ✅ Collect training data from RX logs (Task 2.2)
3. ✅ Implement alert system (Task 2.4)
4. ✅ Test anomaly detection with simulated attacks

#### **Week 4:**
1. ✅ Integrate k-NN alerts into Firestore (Task 2.5)
2. ✅ Setup Firebase Cloud Messaging for mobile alerts (Task 2.6)
3. ✅ Full system testing & validation (Task 2.7)
4. ✅ Deploy to production

---

### **Deliverables Checklist**

#### **RX_ESP32_Firestore Folder:**
- [x] `platformio.ini` - project config
- [x] `src/main.cpp` - main entry point + loop
- [x] `src/config.h` - constants + pins
- [x] `src/serial_reader.h/cpp` - parse RX gateway output
- [x] `src/json_parser.h/cpp` - parse JSON payload
- [x] `src/firestore_client.h/cpp` - Firestore upload
- [x] `src/wifi_manager.h/cpp` - WiFi handling
- [x] `.gitignore` - exclude firestore_secrets.h
- [x] `README.md` - setup instructions

#### **knn_anomaly_detector Folder:**
- [x] `knn_anomaly_detector.py` - main script
- [x] `training_data.csv` - normal packet observations
- [x] `requirements.txt` - Python dependencies (scikit-learn, firebase-admin)
- [x] `config.py` - thresholds, k value, feature weights
- [x] `README.md` - usage instructions

#### **Testing:**
- [x] Serial parsing tests (unit tests for readRXPacket)
- [x] JSON parsing tests (unit tests for parsePayload)
- [x] Firestore integration tests (mock Firebase)
- [x] k-NN accuracy tests (confusion matrix, ROC curve)
- [x] End-to-end integration test (live RX packets → Firestore → alerts)

---

**Ready for AI to begin implementation!** 🚀

For questions on specific tasks, refer to the detailed sections above. All dependencies, file structures, and code examples are provided.
