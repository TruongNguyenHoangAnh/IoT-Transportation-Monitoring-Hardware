#pragma once

#include <Arduino.h>

String encryptDataToAESBase64(const String& jsonStr);
String hmacSha256(const String& message);
