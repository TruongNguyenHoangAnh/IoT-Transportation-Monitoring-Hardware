#ifndef SECURITY_MODULE_H
#define SECURITY_MODULE_H

#include <Arduino.h>

String encryptDataToAESBase64(const String& jsonStr);
String hmacSha256(const String& message);

#endif // SECURITY_MODULE_H
