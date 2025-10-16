# Dự án DATN: Hệ thống IoT Quan trắc Cảm biến (PlatformIO)

Đây là một dự án tốt nghiệp (DATN) sử dụng ESP32 để thu thập dữ liệu từ cảm biến và hiển thị lên dashboard thời gian thực. README này mô tả cấu trúc dự án, cách build, nạp và cách chạy dashboard.

## Cấu trúc thư mục

```
DATN/
├── platformio.ini       # File cấu hình PlatformIO (env: nodemcu-32s)
├── src/
│   └── main.cpp         # Mã nguồn chính cho ESP32
├── include/             # Header files (nếu có)
├── lib/                 # Thư viện cục bộ (nếu có)
├── test/                # Unit tests hoặc scripts kiểm thử
└── README.md            # File tài liệu này
```

## Phần cứng (ví dụ)

- Board ESP32 (NodeMCU-32S hoặc tương đương)
- Cảm biến nhiệt độ/độ ẩm (DHT11/DHT22)
- Cảm biến ánh sáng (LDR + điện trở)
- Dây cắm, breadboard, USB cable

## Yêu cầu phần mềm

- Visual Studio Code
- PlatformIO IDE extension (trong VS Code)
- Python (nếu cần chạy local server)

## Cấu hình PlatformIO

Env mặc định trong `platformio.ini`:

```ini
[env:nodemcu-32s]
platform = espressif32
board = nodemcu-32s
framework = arduino
lib_deps = adafruit/DHT sensor library@^1.4.6
```

Bạn có thể thêm hoặc chỉnh `upload_speed`, `board_build.flash_mode` hoặc các `lib_deps` khác nếu cần.

## Build & Upload

Từ Visual Studio Code:
- Mở thư mục `DATN`.
- Dùng các nút PlatformIO ở thanh trạng thái: Build / Upload / Monitor.

Từ terminal (PowerShell):

```powershell
# Build
platformio run --environment nodemcu-32s

# Upload
platformio run --target upload --environment nodemcu-32s

# Monitor serial
platformio device monitor --environment nodemcu-32s
```

## Dashboard (frontend)

Nếu dự án có dashboard HTML, bạn có thể phục vụ nó bằng một local server hoặc mở trực tiếp trong trình duyệt (khuyến nghị dùng server để tránh vấn đề CORS):

- Dùng Live Server (VS Code) → mở `server/index.html` tại http://127.0.0.1:5500/server/index.html
- Hoặc dùng Python simple server từ thư mục chứa `server`:

```powershell
cd <path-to-project>\DATN\server
python -m http.server 8000
# Mở http://localhost:8000/index.html
```

## Lưu ý & Troubleshooting

- Nếu upload gặp lỗi (ví dụ flash id = 0xffff): thử giảm `upload_speed`, kiểm tra chế độ boot (GPIO0), thử cáp USB khác.
- Nếu dashboard trên trình duyệt không hiển thị dữ liệu: kiểm tra địa chỉ IP thiết bị, kiểm tra firewall, hoặc bật MQTT broker nếu dùng MQTT.
- Dùng `platformio run -v` để có đầu ra chi tiết khi gặp lỗi build.

---

Nếu bạn muốn, tôi có thể:
- Thêm file `run_local_server.ps1` để khởi động server tĩnh nhanh trên Windows.
- Mở rộng README với flow diagram, sơ đồ nối dây, hoặc ví dụ JSON trả về từ `/data`.
