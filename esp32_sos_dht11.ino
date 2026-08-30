/* ============================================================
   ThermaSafe - ESP32 + DHT11 temperature sensor + SOS button
   ------------------------------------------------------------
   Sends worker temperature/humidity to Supabase every few seconds,
   and an instant SOS alert when the button is pressed.

   Required libraries (Arduino IDE -> Library Manager):
     - "DHT sensor library" (Adafruit)
     - "Adafruit Unified Sensor"

   NOTE: all text here is ASCII on purpose (Arabic inside code can
   break when copied from chat). Worker names show in Arabic from
   the app database, not from here.
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "DHT.h"

// ---- 1) WiFi (edit these) ----
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---- 2) Supabase (ready) ----
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0";

// ---- 3) Worker identity (edit serial to match a worker in the app) ----
const char* WORKER_SERIAL = "TH-20481";
const char* WORKER_NAME   = "Worker";              // app shows the real Arabic name by serial
const char* WORKER_LOC    = "Production Unit 3";

// ---- 4) Pins ----
const int BUTTON_PIN = 4;    // button between GPIO4 and GND
const int LED_PIN    = 2;    // onboard LED (optional)
const int DHT_PIN    = 15;   // DHT11 DATA on GPIO15
#define   DHT_TYPE   DHT11

// ---- Battery (optional) ----
const int   BATTERY_PIN  = 34;    // ADC pin, battery via voltage divider
const float BATT_DIVIDER = 2.0;   // divider ratio (e.g. two equal resistors = 2.0)
const float BATT_FULL    = 4.2;   // volts = 100%
const float BATT_EMPTY   = 3.3;   // volts = 0%
int TEST_BATTERY = -1;            // no battery hardware? set 0..100 (e.g. 15) to test the low warning

DHT dht(DHT_PIN, DHT_TYPE);

int readBattery() {
  if (TEST_BATTERY >= 0) return TEST_BATTERY;
  int raw = analogRead(BATTERY_PIN);
  float v = (raw / 4095.0) * 3.3 * BATT_DIVIDER;
  int pct = (int)((v - BATT_EMPTY) / (BATT_FULL - BATT_EMPTY) * 100.0);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
}

bool lastBtn = HIGH;
unsigned long lastSend = 0;
unsigned long lastTemp = 0;
unsigned long lastRetry = 0;
const unsigned long TEMP_EVERY = 4000;   // send temperature every 4 seconds

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(400); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi FAILED. Nearby 2.4GHz networks:");
    int n = WiFi.scanNetworks();
    if (n == 0) Serial.println("  (none found - are you near a 2.4GHz router?)");
    for (int i = 0; i < n; i++) Serial.printf("  '%s'  RSSI %d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    Serial.println("Check: SSID spelling, password (case-sensitive), and 2.4GHz (not 5GHz).");
  }
}

int postJSON(String path, String body) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https;
  https.begin(client, String(SUPABASE_URL) + path);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Prefer", "return=minimal");
  int code = https.POST(body);
  if (code <= 0) Serial.println("Connection error: " + https.errorToString(code));
  https.end();
  return code;
}

void sendTemperature() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) { Serial.println("DHT11 read failed (check wiring)."); return; }
  int batt = readBattery();
  String body = "{\"p_serial\":\"" + String(WORKER_SERIAL) +
                "\",\"p_temp\":" + String(t, 1) +
                ",\"p_hum\":" + String(h, 0) +
                ",\"p_batt\":" + String(batt) + "}";
  int code = postJSON("/rest/v1/rpc/ts_update_temp", body);
  Serial.printf("Temp %.1fC  Hum %.0f%%  Batt %d%%  -> HTTP %d\n", t, h, batt, code);
}

void sendSOS() {
  String body = "{\"serial\":\"" + String(WORKER_SERIAL) +
                "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";
  int code = postJSON("/rest/v1/sos_alerts", body);
  Serial.printf("SOS sent -> HTTP %d %s\n", code, (code==201||code==204)?"OK":"CHECK");
}

void setup() {
  Serial.begin(115200); delay(300);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  dht.begin();
  connectWiFi();
  Serial.println("Ready. Temperature auto-sends, button sends SOS.");
}

void loop() {
  // keep WiFi alive; retry every 15s if disconnected (no error spam)
  if (WiFi.status() != WL_CONNECTED && millis() - lastRetry > 15000) { lastRetry = millis(); connectWiFi(); }

  if (WiFi.status() == WL_CONNECTED && millis() - lastTemp > TEMP_EVERY) { lastTemp = millis(); sendTemperature(); }

  bool b = digitalRead(BUTTON_PIN);
  if (lastBtn == HIGH && b == LOW && millis() - lastSend > 3000) {
    lastSend = millis();
    Serial.println("Emergency button pressed!");
    digitalWrite(LED_PIN, HIGH);
    sendSOS();
    delay(400);
    digitalWrite(LED_PIN, LOW);
  }
  lastBtn = b;
  delay(20);
}

/* ============================================================
   DHT11 wiring:  VCC -> 3V3 , DATA -> GPIO15 , GND -> GND
   Button wiring: one leg -> GPIO4 , other leg -> GND
   For ESP8266: swap the first 3 headers to ESP8266WiFi.h /
   WiFiClientSecure.h / ESP8266HTTPClient.h, and set DHT_PIN (e.g. D4).
   ============================================================ */
