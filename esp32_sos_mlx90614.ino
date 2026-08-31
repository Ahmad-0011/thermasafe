/* ============================================================
   ThermaSafe - ESP32 + MLX90614 (infrared body temperature) + SOS
   ------------------------------------------------------------
   Measures the worker's body temperature (non-contact IR) and sends
   it to Supabase; SOS button sends an instant alert.
   WiFi is configured from the Serial Monitor and SAVED to flash.

   Libraries (Arduino IDE -> Library Manager):
     - "Adafruit MLX90614 Library"
     - "Adafruit BusIO"
   Serial Monitor: 115200 baud, line ending = "Newline".
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>

// ---- Supabase (ready) ----
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0";

// ---- Worker identity (this device) ----
const char* WORKER_SERIAL = "TH-20481";
const char* WORKER_NAME   = "Worker";
const char* WORKER_LOC    = "Production Unit 3";

// ---- Pins ----
const int BUTTON_PIN = 4;    // SOS button between GPIO4 and GND
const int LED_PIN    = 2;    // onboard LED (optional)
// MLX90614 uses I2C: SDA -> GPIO21 , SCL -> GPIO22 (ESP32 defaults)

// ---- Battery (optional) ----
const int   BATTERY_PIN  = 34;
const float BATT_DIVIDER = 2.0;
const float BATT_FULL    = 4.2;
const float BATT_EMPTY   = 3.3;
int TEST_BATTERY = -1;   // set 0..100 (e.g. 15) to test the low-battery warning without hardware

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
Preferences prefs;
String wifiSSID = "", wifiPASS = "";
bool  mlxOK = false;

bool lastBtn = HIGH;
unsigned long lastSend = 0, lastTemp = 0, lastRetry = 0;
const unsigned long TEMP_EVERY = 4000;

String readLine() {
  while (Serial.available() == 0) { delay(50); }
  delay(60); String s = Serial.readStringUntil('\n'); s.trim(); return s;
}
void loadCreds() { prefs.begin("thermasafe", true); wifiSSID = prefs.getString("ssid", ""); wifiPASS = prefs.getString("pass", ""); prefs.end(); }
void saveCreds(String s, String p) { prefs.begin("thermasafe", false); prefs.putString("ssid", s); prefs.putString("pass", p); prefs.end(); wifiSSID = s; wifiPASS = p; }

bool tryConnect(uint32_t timeoutMs) {
  if (wifiSSID == "") return false;
  WiFi.disconnect(true, true); delay(200); WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  Serial.print("Connecting to '"); Serial.print(wifiSSID); Serial.print("' ");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) { delay(400); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) { Serial.println("\nConnected. IP: " + WiFi.localIP().toString()); return true; }
  Serial.println("\nCould not connect."); return false;
}
void serialWifiSetup() {
  Serial.println("\n=== WiFi Setup ===\nScanning networks...");
  WiFi.mode(WIFI_STA); WiFi.disconnect(true, true); delay(300);
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("No networks (need 2.4GHz). Press RESET."); return; }
  for (int i = 0; i < n; i++) Serial.printf("  %2d)  %-26s  RSSI %d\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  Serial.println("\nType the NUMBER of your network + Enter:");
  int idx = readLine().toInt();
  if (idx < 0 || idx >= n) { Serial.println("Invalid. Send 'c' to retry."); return; }
  String ssid = WiFi.SSID(idx);
  Serial.print("Selected: "); Serial.println(ssid);
  Serial.println("Type the PASSWORD + Enter:");
  String pass = readLine();
  saveCreds(ssid, pass);
  if (tryConnect(15000)) Serial.println("Saved. Auto-connects from now on.");
  else Serial.println("Wrong password or 5GHz. Send 'c' to retry.");
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
  if (!mlxOK) { Serial.println("MLX90614 not found (check I2C wiring)."); return; }
  float body = mlx.readObjectTempC();     // worker body temperature (non-contact)
  if (isnan(body) || body < -20 || body > 200) { Serial.println("MLX read invalid."); return; }
  int batt = readBattery();
  String json = "{\"p_serial\":\"" + String(WORKER_SERIAL) + "\",\"p_temp\":" + String(body, 1) +
                ",\"p_batt\":" + String(batt) + "}";
  int code = postJSON("/rest/v1/rpc/ts_update_temp", json);
  Serial.printf("Body %.1fC  Batt %d%%  -> HTTP %d\n", body, batt, code);
}

void sendSOS() {
  String json = "{\"serial\":\"" + String(WORKER_SERIAL) + "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";
  int code = postJSON("/rest/v1/sos_alerts", json);
  Serial.printf("SOS sent -> HTTP %d %s\n", code, (code==201||code==204)?"OK":"CHECK");
}

void setup() {
  Serial.begin(115200); delay(600); Serial.setTimeout(30000);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  Wire.begin();                 // SDA=GPIO21, SCL=GPIO22 by default
  mlxOK = mlx.begin();
  Serial.println(mlxOK ? "MLX90614 ready." : "MLX90614 NOT found - check SDA/SCL/power.");
  loadCreds();

  Serial.println("Send 'c' + Enter within 4 seconds to configure WiFi.");
  unsigned long t0 = millis(); bool cfg = false;
  while (millis() - t0 < 4000) { if (Serial.available()) { String s = Serial.readStringUntil('\n'); s.trim(); if (s=="c"||s=="C") cfg=true; break; } delay(50); }
  if (cfg || wifiSSID == "") serialWifiSetup();
  else if (!tryConnect(15000)) { Serial.println("Saved WiFi failed - opening setup."); serialWifiSetup(); }
  Serial.println("Ready. (Send 'c' anytime to change WiFi.)");
}

void loop() {
  if (Serial.available()) { String s = Serial.readStringUntil('\n'); s.trim(); if (s=="c"||s=="C") serialWifiSetup(); }
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

/* ============================================================
   MLX90614 (GY-906) wiring:
     VIN/VCC -> 3V3 , GND -> GND , SDA -> GPIO21 , SCL -> GPIO22
   Button: one leg -> GPIO4 , other leg -> GND
   Point the sensor at the skin/forehead (~2-5 cm) for body temp.
   ============================================================ */
