/* ============================================================
   ThermaSafe — ESP32 + حساس حرارة DHT11 + زر طوارئ SOS
   ------------------------------------------------------------
   • يقرأ الحرارة/الرطوبة من DHT11 ويرسلها دورياً لقاعدة البيانات
     (تظهر في صفحة العامل جنب اسمه، وفي جدول المختص مع تحذير عند تجاوز الحد).
   • زر الطوارئ يرسل نداء SOS فوري.

   المكتبات المطلوبة (Arduino IDE → Library Manager، ابحث وثبّت):
     - "DHT sensor library" (Adafruit)
     - "Adafruit Unified Sensor"
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "DHT.h"

// ------------------ 1) إعدادات الشبكة (عدّلها) ------------------
const char* WIFI_SSID = "اسم_شبكة_الواي_فاي";
const char* WIFI_PASS = "كلمة_مرور_الواي_فاي";

// ------------------ 2) إعدادات Supabase (جاهزة) ------------------
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0";

// ------------------ 3) هوية العامل/الخوذة (عدّلها) ------------------
const char* WORKER_SERIAL = "TH-13457";
const char* WORKER_NAME   = "عامل الميدان";
const char* WORKER_LOC    = "وحدة الإنتاج 3";

// ------------------ 4) الأطراف (Pins) ------------------
const int BUTTON_PIN = 4;    // زر الطوارئ بين GPIO4 و GND
const int LED_PIN    = 2;    // LED مدمج (اختياري)
const int DHT_PIN    = 15;   // طرف بيانات DHT11 على GPIO15
#define   DHT_TYPE   DHT11

DHT dht(DHT_PIN, DHT_TYPE);

bool lastBtn = HIGH;
unsigned long lastSend = 0;
unsigned long lastTemp = 0;
const unsigned long TEMP_EVERY = 15000;   // إرسال الحرارة كل 15 ثانية

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("الاتصال بالشبكة");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(400); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\n✅ متصل. IP: " + WiFi.localIP().toString());
  else Serial.println("\n❌ فشل الاتصال بالشبكة.");
}

// إرسال طلب POST إلى Supabase (REST أو RPC)
int postJSON(String path, String body) {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient https;
  https.begin(client, String(SUPABASE_URL) + path);
  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Prefer", "return=minimal");
  int code = https.POST(body);
  if (code <= 0) Serial.println("خطأ اتصال: " + https.errorToString(code));
  https.end();
  return code;
}

// إرسال الحرارة والرطوبة (دالة ts_update_temp)
void sendTemperature() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) { Serial.println("⚠️ تعذّرت قراءة DHT11 (تحقق من التوصيل)."); return; }
  String body = "{\"p_serial\":\"" + String(WORKER_SERIAL) +
                "\",\"p_temp\":" + String(t, 1) +
                ",\"p_hum\":" + String(h, 0) + "}";
  int code = postJSON("/rest/v1/rpc/ts_update_temp", body);
  Serial.printf("🌡️ حرارة %.1f°C ورطوبة %.0f%% — استجابة: %d\n", t, h, code);
}

// إرسال نداء الطوارئ (جدول sos_alerts)
void sendSOS() {
  String body = "{\"serial\":\"" + String(WORKER_SERIAL) +
                "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";
  int code = postJSON("/rest/v1/sos_alerts", body);
  Serial.printf("📡 إرسال SOS — استجابة: %d %s\n", code, (code==201||code==204)?"✅":"⚠️");
}

void setup() {
  Serial.begin(115200); delay(300);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  dht.begin();
  connectWiFi();
  Serial.println("جاهز. الحرارة تُرسل تلقائياً، والزر يرسل SOS.");
}

void loop() {
  // 1) إرسال الحرارة دورياً
  if (millis() - lastTemp > TEMP_EVERY) { lastTemp = millis(); sendTemperature(); }

  // 2) زر الطوارئ (ضغطة + مانع تكرار 3 ثوانٍ)
  bool b = digitalRead(BUTTON_PIN);
  if (lastBtn == HIGH && b == LOW && millis() - lastSend > 3000) {
    lastSend = millis();
    Serial.println("🔴 تم الضغط على زر الطوارئ!");
    digitalWrite(LED_PIN, HIGH);
    sendSOS();
    delay(400);
    digitalWrite(LED_PIN, LOW);
  }
  lastBtn = b;
  delay(20);
}

/* ============================================================
   توصيل DHT11 (3 أرجل عادةً، أو 4):
     VCC  → 3V3 في ESP32
     DATA → GPIO15
     GND  → GND
   (بعض وحدات DHT11 فيها مقاومة مدمجة؛ إن لم تكن، ضع مقاومة 10kΩ
    بين DATA و VCC.)

   لـ ESP8266: بدّل أول 3 ترويسات إلى ESP8266WiFi.h / WiFiClientSecure.h /
   ESP8266HTTPClient.h، وغيّر DHT_PIN إلى منفذ مناسب (مثل D4).
   ============================================================ */
