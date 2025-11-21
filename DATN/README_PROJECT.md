# DATN — Project configuration and usage

This document explains how to build, upload, and run the DATN project (ESP32, PlatformIO) and summarizes the project configuration.
# DATN — Mô tả cấu trúc thư mục

Tập tin này mô tả cấu trúc thư mục chính của dự án DATN và mục đích của từng thư mục chính.

```
DATN/
├── platformio.ini       # Cấu hình PlatformIO (env, board, lib_deps)
├── src/                 # Mã nguồn chính (ví dụ: main.cpp)
│   └── main.cpp
├── include/             # Header files (.h) nếu có
├── lib/                 # Thư viện địa phương (custom libs)
├── test/                # Unit tests hoặc scripts kiểm thử
├── data/                # (tùy chọn) mẫu dữ liệu, JSON mẫu, tài nguyên
└── README_PROJECT.md    # File mô tả cấu trúc (file hiện tại)
```

Giải thích nhanh cho từng mục:
- `platformio.ini`: file cấu hình chính cho PlatformIO. Định nghĩa các env, board, framework và `lib_deps`.
- `src/`: chứa mã nguồn cho vi điều khiển (ESP32). Thông thường đặt `main.cpp` tại đây.
- `include/`: chứa các header dùng chung khi chia code thành module.
- `lib/`: chứa thư viện do bạn viết hoặc thư viện không quản lý bằng `lib_deps`.
- `test/`: chứa unit test hoặc các script test.
- `data/`: (tùy chọn) chứa mẫu dữ liệu, JSON mẫu, hoặc các tệp tĩnh.
- `README_PROJECT.md`: tài liệu ngắn mô tả cấu trúc dự án (bạn đang đọc nó).

Gợi ý:
- Nếu dự án có phần web/dashboard, tạo thư mục `server/` chứa `index.html`, CSS và JS.
- Thêm nhiều env trong `platformio.ini` nếu bạn muốn build cho nhiều board (ví dụ `esp32dev`, `nodemcu-32s`).

Muốn thêm nữa?
- Tôi có thể chèn sơ đồ nối dây, ví dụ cấu hình `platformio.ini` bổ sung, hoặc một file script cho chạy local server trên Windows.
Command line (Windows PowerShell):
