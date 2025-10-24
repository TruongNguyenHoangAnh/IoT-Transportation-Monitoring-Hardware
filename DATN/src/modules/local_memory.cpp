// #include "local_memory.h"

// File dataFile;

// void initSDCard() {
//   if (!SD.begin(5)) {  // CS pin 5 cho thẻ SD
//     Serial.println("Không thể khởi tạo thẻ SD");
//     return;
//   }
//   Serial.println("Thẻ SD đã được khởi tạo.");
//   dataFile = SD.open("data.csv", FILE_WRITE);
//   if (dataFile) {
//     // Ghi tiêu đề vào file
//     dataFile.println("Timestamp,Latitude,Longitude,Temperature,Humidity,AccelX,AccelY,AccelZ");
//     dataFile.close();
//   } else {
//     Serial.println("Không thể mở file để ghi dữ liệu");
//   }
// }

// void writeDataToSD(unsigned long timestamp, float lat, float lon, float temp, float hum, float ax, float ay, float az) {
//   dataFile = SD.open("data.csv", FILE_WRITE);
//   if (dataFile) {
//     // Ghi dữ liệu vào file
//     dataFile.print(timestamp);
//     dataFile.print(",");
//     dataFile.print(lat, 6);  // In ra vĩ độ với độ chính xác 6 chữ số
//     dataFile.print(",");
//     dataFile.print(lon, 6);  // In ra kinh độ với độ chính xác 6 chữ số
//     dataFile.print(",");
//     dataFile.print(temp);    // Nhiệt độ
//     dataFile.print(",");
//     dataFile.print(hum);     // Độ ẩm
//     dataFile.print(",");
//     dataFile.print(ax);      // Gia tốc X
//     dataFile.print(",");
//     dataFile.print(ay);      // Gia tốc Y
//     dataFile.print(",");
//     dataFile.println(az);    // Gia tốc Z
//     dataFile.close();
//   } else {
//     Serial.println("Không thể mở file để ghi dữ liệu");
//   }
// }
