
#ifndef GPS_NEO6M_H
#define GPS_NEO6M_H


#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SD.h>



class GPSNeo6M {
public:
	GPSNeo6M(int rxPin, int txPin, long baud = 9600);
	void begin();
	void read();
	void printLocation();
	void printTimestamp();
	void logToSD();
private:
	int _rxPin, _txPin;
	long _baud;
	TinyGPSPlus _gps;
	HardwareSerial _serial;
	File _logFile;
	bool _sdReady = false;
};

#endif // GPS_NEO6M_H