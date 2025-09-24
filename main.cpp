#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>

// ====== WIRELESS SETTINGS ======
#define WIFI_SSID      "abc"
#define WIFI_PASSWORD  "123"

// ====== FIREBASE SETTINGS ======
#define FIREBASE_HOST  "conect-sist-default-rtdb.firebaseio.com/motions.json"
#define FIREBASE_AUTH  "FIREBASE_DATABASE_SECRET_OR_TOKEN"

const int PIR_PIN = 15;
const unsigned long MIN_SEND_INTERVAL = 2000;

const long BR_TIMEZONE_OFFSET_SECONDS = -3 * 3600;
const long TZ_OFFSET_SECONDS = BR_TIMEZONE_OFFSET_SECONDS;
const char* NTP_POOL = "pool.ntp.org";

// =============================
FirebaseData fbdo;
FirebaseConfig config;

unsigned long lastSend = 0;
int lastPirState = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // ms

void initTime() {
  configTime(TZ_OFFSET_SECONDS, 0, NTP_POOL);

  struct tm timeinfo;
  for (int i = 0; i < 10; ++i) {
    if (getLocalTime(&timeinfo)) {
      Serial.println("Hora sincronizada via NTP.");
      break;
    }
    Serial.println("Aguardando NTP...");
    delay(1000);
  }
}

String getISOTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String("0000-00-00T00:00:00Z");
  }
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);

  String tz = "-03:00";
  return String(buf) + tz;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  delay(100);

  Serial.printf("Conectando WiFi %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifiRetry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    wifiRetry++;
    if (wifiRetry > 60) { // 30s timeout
      Serial.println("\nFalha ao conectar WiFi. Reiniciando...");
      ESP.restart();
    }
  }
  Serial.println("\nWiFi conectado.");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  initTime();

  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH; // usa token legado / secret
  Firebase.begin(&config, &fbdo);
  Firebase.reconnectWiFi(true);

  Serial.println("Setup completo.");
}

void loop() {
  int reading = digitalRead(PIR_PIN);

  if (reading != lastPirState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastPirState) {
      lastPirState = reading;
      sendMotion(reading);
    }
  }

  delay(10);
}

void sendMotion(int pirValue) {
  unsigned long now = millis();
  if (now - lastSend < MIN_SEND_INTERVAL) {
    return;
  }
  lastSend = now;

  String isoTime = getISOTime();
  bool motion = (pirValue == HIGH);

  FirebaseJson json;
  json.set("/timestamp", isoTime);
  json.set("/motion", motion);
  json.set("/pin", PIR_PIN);
  json.set("/rawValue", pirValue);

  String dbPath = "/motions";

  Serial.printf("Enviando -> time: %s  motion: %s\n", isoTime.c_str(), motion ? "true" : "false");

  if (Firebase.pushJSON(fbdo, dbPath.c_str(), &json)) {
    Serial.println("Sucesso: evento gravado no Firebase.");
    String pushKey = fbdo.pushName();
    Serial.printf("pushName: %s\n", pushKey.c_str());
  } else {
    Serial.printf("Erro ao enviar: %s\n", fbdo.errorReason().c_str());
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi desconectado. Tentando reconectar...");
      WiFi.reconnect();
    }
  }
}
