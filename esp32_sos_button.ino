/* ============================================================
   ThermaSafe - ESP32 emergency SOS button (button only)
   ------------------------------------------------------------
   Sends an SOS alert to Supabase when the button is pressed.
   All text is ASCII (Arabic inside code can break when copied).
   For temperature too, use esp32_sos_dht11.ino instead.
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---- 1) WiFi (edit) ----
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---- 2) Supabase (ready) ----
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0";

// ---- 3) Worker identity ----
const char* WORKER_SERIAL = "TH-20481";
const char* WORKER_NAME   = "Worker";              // app shows real Arabic name by serial
const char* WORKER_LOC    = "Production Unit 3";

// ---- 4) Pins ----
const int BUTTON_PIN = 4;    // button between GPIO4 and GND
const int LED_PIN    = 2;    // onboard LED (optional)

bool lastBtn = HIGH;
unsigned long lastSend = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(400); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nConnected. IP: " + WiFi.localIP().toString());
  else Serial.println("\nWiFi connection failed.");
}

void sendSOS() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https;
  https.begin(client, String(SUPABASE_URL) + "/rest/v1/sos_alerts");
  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Prefer", "return=minimal");
  String body = "{\"serial\":\"" + String(WORKER_SERIAL) +
                "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";
  int code = https.POST(body);
  Serial.printf("SOS sent -> HTTP %d %s\n", code, (code==201||code==204)?"OK":"CHECK");
  https.end();
}

void setup() {
  Serial.begin(115200); delay(300);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  connectWiFi();
  Serial.println("Ready. Press the button to send SOS.");
}

void loop() {
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
