/* ============================================================
   ThermaSafe — زر الطوارئ (SOS) على ESP32
   ------------------------------------------------------------
   عند الضغط على الزر، يرسل ESP32 طلباً مباشرة إلى قاعدة بيانات
   Supabase (جدول sos_alerts)، فيظهر التنبيه فوراً في تطبيق
   مختص السلامة عبر المزامنة اللحظية (Realtime).

   لوحة: ESP32 (Dev Module)
   المكتبات: مدمجة مع حزمة ESP32 (WiFi, WiFiClientSecure, HTTPClient)

   ⚠️ ملاحظة: هذا كود ESP32. لـ ESP8266 انظر التعليقات في الأسفل.
   ============================================================ */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ------------------ 1) إعدادات الشبكة (عدّلها) ------------------
const char* WIFI_SSID = "اسم_شبكة_الواي_فاي";
const char* WIFI_PASS = "كلمة_مرور_الواي_فاي";

// ------------------ 2) إعدادات Supabase (جاهزة) ------------------
const char* SUPABASE_URL = "https://uwpycowwylwjwezzxdfk.supabase.co";
const char* SUPABASE_KEY = "sb_publishable_bTFs7B21uBVzGo7SrhGfMQ_QfZydCA0"; // مفتاح publishable آمن

// ------------------ 3) هوية العامل/الخوذة (عدّلها) ------------------
const char* WORKER_SERIAL = "TH-13457";
const char* WORKER_NAME   = "عامل الميدان";
const char* WORKER_LOC    = "وحدة الإنتاج 3";

// ------------------ 4) الأطراف (Pins) ------------------
const int BUTTON_PIN = 4;    // زر بين GPIO4 و GND (يستخدم المقاومة الداخلية)
const int LED_PIN    = 2;    // LED مدمج للتأكيد البصري (اختياري)

bool lastState = HIGH;
unsigned long lastSend = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("الاتصال بالشبكة");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(400); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("\n✅ متصل. IP: " + WiFi.localIP().toString());
  else
    Serial.println("\n❌ فشل الاتصال بالشبكة.");
}

void sendSOS() {
  if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure();                  // تخطّي التحقق من الشهادة (كافٍ للتجربة)

  HTTPClient https;
  String url = String(SUPABASE_URL) + "/rest/v1/sos_alerts";
  if (!https.begin(client, url)) { Serial.println("تعذّر بدء الطلب."); return; }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("apikey", SUPABASE_KEY);
  https.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  https.addHeader("Prefer", "return=minimal");

  String body = String("{\"serial\":\"") + WORKER_SERIAL +
                "\",\"name\":\"" + WORKER_NAME +
                "\",\"location\":\"" + WORKER_LOC + "\"}";

  int code = https.POST(body);
  Serial.printf("📡 إرسال SOS — كود الاستجابة: %d\n", code);
  if (code == 201 || code == 200 || code == 204) Serial.println("✅ وصل التنبيه للتطبيق.");
  else Serial.println("⚠️ استجابة غير متوقعة: " + https.getString());
  https.end();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(BUTTON_PIN, INPUT_PULLUP);     // الزر مضغوط = LOW
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  connectWiFi();
  Serial.println("جاهز. اضغط زر الطوارئ لإرسال SOS.");
}

void loop() {
  bool s = digitalRead(BUTTON_PIN);
  // اكتشاف ضغطة (حافة هابطة) + مانع تكرار 3 ثوانٍ
  if (lastState == HIGH && s == LOW && millis() - lastSend > 3000) {
    lastSend = millis();
    Serial.println("🔴 تم الضغط على زر الطوارئ!");
    digitalWrite(LED_PIN, HIGH);
    sendSOS();
    delay(400);
    digitalWrite(LED_PIN, LOW);
  }
  lastState = s;
  delay(20);   // debounce بسيط
}

/* ============================================================
   لـ ESP8266 (بدل ESP32) استبدل الترويسات الثلاث الأولى بـ:
     #include <ESP8266WiFi.h>
     #include <WiFiClientSecure.h>
     #include <ESP8266HTTPClient.h>
   وبقية الكود يعمل كما هو (BUTTON_PIN مثلاً D2 = GPIO4، LED_PIN = LED_BUILTIN).
   ============================================================ */
