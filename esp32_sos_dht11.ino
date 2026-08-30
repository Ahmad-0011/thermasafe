/* ============================================================
   ThermaSafe - ESP32 + DHT11 + SOS button
   WiFi is configured from the Serial Monitor and SAVED to flash.
   -> Upload once. First boot asks you to pick a network + password.
      After that it connects automatically on every boot.
      To change WiFi later: send 'c' in the Serial Monitor anytime.

   Libraries: "DHT sensor library" + "Adafruit Unified Sensor"
   Serial Monitor: 115200 baud, line ending = "Newline".
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "DHT.h"

// ---- Supabase (ready) ----
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0";

// ---- Worker identity (this device) ----
const char* WORKER_SERIAL = "TH-20481";
const char* WORKER_NAME   = "Worker";
const char* WORKER_LOC    = "Production Unit 3";

// ---- Pins ----
const int BUTTON_PIN = 4;
const int LED_PIN    = 2;
const int DHT_PIN    = 15;
#define   DHT_TYPE   DHT11

// ---- Battery (optional) ----
const int   BATTERY_PIN  = 34;
const float BATT_DIVIDER = 2.0;
const float BATT_FULL    = 4.2;
const float BATT_EMPTY   = 3.3;
int TEST_BATTERY = -1;   // set 0..100 (e.g. 15) to test the low-battery warning without hardware

DHT dht(DHT_PIN, DHT_TYPE);
Preferences prefs;
String wifiSSID = "";
String wifiPASS = "";

bool lastBtn = HIGH;
unsigned long lastSend = 0, lastTemp = 0, lastRetry = 0;
const unsigned long TEMP_EVERY = 4000;

String readLine() {
  while (Serial.available() == 0) { delay(50); }
  delay(60);
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s;
}

void loadCreds() {
  prefs.begin("thermasafe", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  prefs.end();
}
void saveCreds(String s, String p) {
  prefs.begin("thermasafe", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", p);
  prefs.end();
  wifiSSID = s; wifiPASS = p;
}

bool tryConnect(uint32_t timeoutMs) {
  if (wifiSSID == "") return false;
  WiFi.disconnect(true, true); delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  Serial.print("Connecting to '"); Serial.print(wifiSSID); Serial.print("' ");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) { delay(400); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) { Serial.println("\nConnected. IP: " + WiFi.localIP().toString()); return true; }
  Serial.println("\nCould not connect.");
  return false;
}

// Serial wizard: scan -> pick -> password -> save -> connect
void serialWifiSetup() {
  Serial.println("\n=== WiFi Setup ===");
  Serial.println("Scanning networks...");
  WiFi.mode(WIFI_STA); WiFi.disconnect(true, true); delay(300);
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("No networks found (need a 2.4GHz network). Press RESET."); return; }
  Serial.println("Nearby networks (2.4GHz only):");
  for (int i = 0; i < n; i++)
    Serial.printf("  %2d)  %-26s  RSSI %d %s\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "(open)" : "");
  Serial.println("\nType the NUMBER of your network + Enter:");
  int idx = readLine().toInt();
  if (idx < 0 || idx >= n) { Serial.println("Invalid number. Send 'c' to retry."); return; }
  String ssid = WiFi.SSID(idx);
  Serial.print("Selected: "); Serial.println(ssid);
  Serial.println("Type the PASSWORD + Enter (empty for open network):");
  String pass = readLine();
  saveCreds(ssid, pass);
  if (tryConnect(15000)) Serial.println("Saved. It will auto-connect from now on.");
  else Serial.println("Wrong password or 5GHz network. Send 'c' to try again.");
}

int readBattery() {
  if (TEST_BATTERY >= 0) return TEST_BATTERY;
  int raw = analogRead(BATTERY_PIN);
  float v = (raw / 4095.0) * 3.3 * BATT_DIVIDER;
  int pct = (int)((v - BATT_EMPTY) / (BATT_FULL - BATT_EMPTY) * 100.0);
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  return pct;
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
  https.end();
  return code;
}

void sendTemperature() {
  float t = dht.readTemperature(), h = dht.readHumidity();
  if (isnan(t) || isnan(h)) { Serial.println("DHT11 read failed (check wiring)."); return; }
  int batt = readBattery();
  String body = "{\"p_serial\":\"" + String(WORKER_SERIAL) + "\",\"p_temp\":" + String(t,1) +
                ",\"p_hum\":" + String(h,0) + ",\"p_batt\":" + String(batt) + "}";
  int code = postJSON("/rest/v1/rpc/ts_update_temp", body);
  Serial.printf("Temp %.1fC  Hum %.0f%%  Batt %d%%  -> HTTP %d\n", t, h, batt, code);
}

void sendSOS() {
  String body = "{\"serial\":\"" + String(WORKER_SERIAL) + "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";
  int code = postJSON("/rest/v1/sos_alerts", body);
  Serial.printf("SOS sent -> HTTP %d %s\n", code, (code==201||code==204)?"OK":"CHECK");
}

void setup() {
  Serial.begin(115200); delay(600);
  Serial.setTimeout(30000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  dht.begin();
  loadCreds();

  Serial.println("\nThermaSafe device starting...");
  Serial.println("Send 'c' + Enter within 4 seconds to configure WiFi.");
  unsigned long t0 = millis(); bool wantCfg = false;
  while (millis() - t0 < 4000) {
    if (Serial.available()) { String s = Serial.readStringUntil('\n'); s.trim(); if (s == "c" || s == "C") wantCfg = true; break; }
    delay(50);
  }

  if (wantCfg || wifiSSID == "") serialWifiSetup();
  else if (!tryConnect(15000)) { Serial.println("Saved WiFi failed - opening setup."); serialWifiSetup(); }

  Serial.println("Ready. (Send 'c' anytime to change WiFi.)");
}

void loop() {
  // allow WiFi reconfigure anytime
  if (Serial.available()) { String s = Serial.readStringUntil('\n'); s.trim(); if (s == "c" || s == "C") serialWifiSetup(); }

  // keep WiFi alive
  if (WiFi.status() != WL_CONNECTED && millis() - lastRetry > 15000) { lastRetry = millis(); tryConnect(10000); }

  if (WiFi.status() == WL_CONNECTED && millis() - lastTemp > TEMP_EVERY) { lastTemp = millis(); sendTemperature(); }

  bool b = digitalRead(BUTTON_PIN);
  if (lastBtn == HIGH && b == LOW && millis() - lastSend > 3000) {
    lastSend = millis();
    Serial.println("Emergency button pressed!");
    digitalWrite(LED_PIN, HIGH); sendSOS(); delay(400); digitalWrite(LED_PIN, LOW);
  }
  lastBtn = b;
  delay(20);
}

/* Wiring:  DHT11 VCC->3V3 DATA->GPIO15 GND->GND ; Button GPIO4<->GND */
