/*
  ESP32 + capacitive soil moisture sensor -> REST API
  Wiring: sensor VCC -> 3V3, GND -> GND, AOUT -> GPIO34
  Board:  "ESP32 Dev Module" in Arduino IDE (no extra libraries needed)
*/
#include <WiFi.h>
#include <HTTPClient.h>

// ---- Edit these ----
const char* WIFI_SSID   = "YOUR_WIFI_NAME";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char* SERVER_URL  = "http://192.168.1.50:5000/api/readings"; // your PC's local IP
const char* API_KEY     = "change-me";                              // same as backend .env
const char* DEVICE_ID   = "esp32-01";
const char* LOCATION    = "Foundation trench";

const int   SENSOR_PIN  = 34;
// Calibrate: raw value with the sensor in open air (dry) and in a glass of water (wet)
const int   RAW_DRY     = 3200;
const int   RAW_WET     = 1400;
const unsigned long SEND_EVERY_MS = 30000;

int readRawAveraged() {
  long sum = 0;
  for (int i = 0; i < 20; i++) { sum += analogRead(SENSOR_PIN); delay(10); }
  return sum / 20;
}

float rawToPercent(int raw) {
  float pct = (float)(RAW_DRY - raw) * 100.0 / (RAW_DRY - RAW_WET);
  return constrain(pct, 0.0, 100.0);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.print("\nConnected, IP: "); Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  connectWifi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();

  int raw = readRawAveraged();
  float moisture = rawToPercent(raw);
  Serial.printf("raw=%d  moisture=%.1f%%\n", raw, moisture);

  char body[200];
  snprintf(body, sizeof(body),
           "{\"deviceId\":\"%s\",\"location\":\"%s\",\"raw\":%d,\"moisture\":%.1f}",
           DEVICE_ID, LOCATION, raw, moisture);

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", API_KEY);
  int code = http.POST(body);
  Serial.printf("POST -> %d\n", code);
  http.end();

  delay(SEND_EVERY_MS);
}
