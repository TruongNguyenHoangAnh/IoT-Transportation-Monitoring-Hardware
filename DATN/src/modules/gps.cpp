#include "gps.h"

GPSModule::GPSModule(HardwareSerial &serial, unsigned long baud)
  : ser(serial), baudRate(baud) {}

void GPSModule::begin() {
  ser.begin(baudRate);
}

void GPSModule::beginPins(int rxPin, int txPin, unsigned long baud) {
  if (baud > 0) baudRate = baud;
  ser.begin(baudRate, SERIAL_8N1, rxPin, txPin);
}

// Start a FreeRTOS task that prints the full example rows periodically
static void gpsFullTask(void *pvParameters) {
  GPSModule *mod = reinterpret_cast<GPSModule*>(pvParameters);
  if (!mod) vTaskDelete(NULL);
  mod->printFullExampleHeader();
  for (;;) {
    mod->printFullExampleRow();
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void startGpsFullTask(GPSModule &mod, unsigned long stackSize = 8192, UBaseType_t priority = 1) {
  xTaskCreate(gpsFullTask, "GPS_FULL", stackSize, &mod, priority, NULL);
}

void GPSModule::loop() {
  while (ser.available()) {
    gps.encode(ser.read());
  }
}

bool GPSModule::hasFix() { return gps.location.isValid(); }

double GPSModule::latitude() { return gps.location.isValid() ? gps.location.lat() : NAN; }

double GPSModule::longitude() { return gps.location.isValid() ? gps.location.lng() : NAN; }

String GPSModule::timestamp() {
  if (!gps.date.isValid() || !gps.time.isValid()) return String();
  char buf[32];
  sprintf(buf, "%04u-%02u-%02uT%02u:%02u:%02uZ", (unsigned)gps.date.year(), (unsigned)gps.date.month(), (unsigned)gps.date.day(), (unsigned)gps.time.hour(), (unsigned)gps.time.minute(), (unsigned)gps.time.second());
  return String(buf);
}

// ---------------- Full example printing helpers ----------------
void GPSModule::printFullExampleHeader() {
  Serial.println(F("FullExample (ESP32 + TinyGPSPlus)"));
  Serial.print(F("TinyGPSPlus v.")); Serial.println(TinyGPSPlus::libraryVersion());
  Serial.println();
  Serial.println(F("Sats HDOP  Latitude   Longitude   Fix  Date       Time     Date Alt    Course Speed Card  Distance Course Card  Chars Sentences Checksum"));
  Serial.println(F("           (deg)      (deg)       Age                      Age  (m)    --- from GPS ----  ---- to London  ----  RX    RX        Fail"));
  Serial.println(F("----------------------------------------------------------------------------------------------------------------------------------------"));
}

void GPSModule::printFullExampleRow() {
  static const double LONDON_LAT = 51.508131, LONDON_LON = -0.128002;
  // Drain serial into gps parser
  while (ser.available()) gps.encode(ser.read());

  printInt(gps.satellites.value(), gps.satellites.isValid(), 5);
  printFloat(gps.hdop.hdop(), gps.hdop.isValid(), 6, 1);
  printFloat(gps.location.lat(), gps.location.isValid(), 11, 6);
  printFloat(gps.location.lng(), gps.location.isValid(), 12, 6);
  printInt(gps.location.age(), gps.location.isValid(), 5);
  printDateTime(gps.date, gps.time);
  printFloat(gps.altitude.meters(), gps.altitude.isValid(), 7, 2);
  printFloat(gps.course.deg(), gps.course.isValid(), 7, 2);
  printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);
  printStr(gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.deg()) : "*** ", 6);

  unsigned long distanceKmToLondon =
    (unsigned long)TinyGPSPlus::distanceBetween(
      gps.location.lat(),
      gps.location.lng(),
      LONDON_LAT,
      LONDON_LON) / 1000;
  printInt(distanceKmToLondon, gps.location.isValid(), 9);

  double courseToLondon = TinyGPSPlus::courseTo(
      gps.location.lat(), gps.location.lng(), LONDON_LAT, LONDON_LON);
  printFloat(courseToLondon, gps.location.isValid(), 7, 2);

  const char *cardinalToLondon = TinyGPSPlus::cardinal(courseToLondon);
  printStr(gps.location.isValid() ? cardinalToLondon : "*** ", 6);

  printInt(gps.charsProcessed(), true, 6);
  printInt(gps.sentencesWithFix(), true, 10);
  printInt(gps.failedChecksum(), true, 9);
  Serial.println();

  smartDelay(1000);

  if (millis() > 5000 && gps.charsProcessed() < 10)
    Serial.println(F("No GPS data received: check wiring"));
}

void GPSModule::smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (ser.available()) gps.encode(ser.read());
  } while (millis() - start < ms);
}

void GPSModule::printFloat(float val, bool valid, int len, int prec) {
  if (!valid) {
    while (len-- > 1) Serial.print('*');
    Serial.print(' ');
  } else {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1);
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i = flen; i < len; ++i) Serial.print(' ');
  }
  smartDelay(0);
}

void GPSModule::printInt(unsigned long val, bool valid, int len) {
  char sz[32] = "*****************";
  if (valid) sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i = strlen(sz); i < len; ++i) sz[i] = ' ';
  if (len > 0) sz[len - 1] = ' ';
  Serial.print(sz);
  smartDelay(0);
}

void GPSModule::printDateTime(TinyGPSDate &d, TinyGPSTime &t) {
  if (!d.isValid())
    Serial.print(F("********** "));
  else {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    Serial.print(sz);
  }

  if (!t.isValid())
    Serial.print(F("******** "));
  else {
    char sz[32];
    sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    Serial.print(sz);
  }

  printInt(d.age(), d.isValid(), 5);
  smartDelay(0);
}

void GPSModule::printStr(const char *str, int len) {
  int slen = strlen(str);
  for (int i = 0; i < len; ++i)
    Serial.print(i < slen ? str[i] : ' ');
  smartDelay(0);
}
