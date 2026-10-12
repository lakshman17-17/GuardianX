#pragma once
#include <Arduino.h>

// GuardianX ESP32 settings - edit these 4 lines
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const String FB_URL = "https://PASTE_PROJECT_ID-default-rtdb.firebaseio.com";  // same as databaseURL in firebase-config.js
const String DEVICE_ID = "gx01";                                               // same as DEVICE_ID in firebase-config.js
