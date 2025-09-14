#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define WIFI_SSID "Safa"
#define WIFI_PASSWORD "2242777***"
#define DATABASE_URL "https://smartlockapp-e22e7-default-rtdb.firebaseio.com"

// منافذ الريلايات والمفتاح الميكروي
#define RELAY1_PIN 14
#define RELAY2_PIN 27
#define RELAY3_PIN 26
#define MICROSWITCH_PIN 4

// منافذ حساس HC-SR04
#define TRIG_PIN 12
#define ECHO_PIN 13

long duration;
float distance;
int lastDistanceState = -1;

// تعريف الجداول
struct RelayCmd {
  const char* cmd;
  uint8_t pin;
};
RelayCmd relayCmds[] = {
  {"unlock1", RELAY1_PIN},
  {"unlock2", RELAY2_PIN},
  {"unlock3", RELAY3_PIN}
};
const int numCmds = sizeof(relayCmds) / sizeof(RelayCmd);

struct BoxRelay {
  const char* boxNumber;
  uint8_t pin;
};
BoxRelay boxRelays[] = {
  {"1", RELAY1_PIN},
  {"2", RELAY2_PIN},
  {"3", RELAY3_PIN}
};
const int numBoxes = sizeof(boxRelays) / sizeof(BoxRelay);

int lastMicroswitchState = HIGH;
String lastUnlockCommandId = "";
String lastRemoteRequestId = "";

// إرسال حالة المفتاح الميكروي
void sendMicroswitchState(int state) {
  String url = String(DATABASE_URL) + "/microswitchState.json";
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String payload = state == LOW ? "\"closed\"" : "\"open\"";
  int httpResponseCode = http.PUT(payload);
  if (httpResponseCode > 0) {
    Serial.print("تم إرسال حالة المفتاح: ");
    Serial.println(payload);
  } else {
    Serial.print("فشل في الإرسال: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// إرسال حالة حساس الموجات
void sendUltrasonicState(int state) {
  String url = String(DATABASE_URL) + "/ultrasonicState.json";
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  String payload;
  if (state == 0)
    payload = "\"key_missing\"";
  else if (state == 1)
    payload = "\"key_present\"";
  else
    payload = "\"unknown\"";

  int httpResponseCode = http.PUT(payload);
  if (httpResponseCode > 0) {
    Serial.print("تم إرسال حالة الحساس: ");
    Serial.println(payload);
  } else {
    Serial.print("فشل في إرسال الحساس: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// قراءة المسافة
float getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 30000);
  distance = duration * 0.034 / 2;
  return distance;
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < numCmds; i++) {
    pinMode(relayCmds[i].pin, OUTPUT);
    digitalWrite(relayCmds[i].pin, LOW);
  }
  for (int i = 0; i < numBoxes; i++) {
    pinMode(boxRelays[i].pin, OUTPUT);
    digitalWrite(boxRelays[i].pin, LOW);
  }

  pinMode(MICROSWITCH_PIN, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  lastMicroswitchState = digitalRead(MICROSWITCH_PIN);
  sendMicroswitchState(lastMicroswitchState);
}

void checkUnlockCommands() {
  String url = String(DATABASE_URL) + "/unlockCommands.json?orderBy=\"$key\"&limitToLast=1";
  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, payload)) {
      for (JsonPair kv : doc.as<JsonObject>()) {
        String key = kv.key().c_str();
        String command = doc[key]["command"].as<String>();
        if (key != lastUnlockCommandId) {
          lastUnlockCommandId = key;
          for (int i = 0; i < numCmds; i++) {
            if (command == relayCmds[i].cmd) {
              Serial.print("تفعيل ريلاي ");
              Serial.println(relayCmds[i].pin);
              digitalWrite(relayCmds[i].pin, HIGH);
              delay(2000);
              digitalWrite(relayCmds[i].pin, LOW);
              break;
            }
          }
        }
      }
    }
  }
  http.end();
}

void checkRemoteUnlockRequests() {
  String url = String(DATABASE_URL) + "/remoteUnlockRequests.json?orderBy=\"$key\"&limitToLast=1";
  HTTPClient http;
  http.begin(url);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    if (!deserializeJson(doc, payload)) {
      for (JsonPair kv : doc.as<JsonObject>()) {
        String requestId = kv.key().c_str();
        String boxNumber = doc[requestId]["boxNumber"].as<String>();
        String command = doc[requestId]["command"].as<String>();
        if (requestId != lastRemoteRequestId) {
          lastRemoteRequestId = requestId;
          if (command.startsWith("unlock")) {
            for (int i = 0; i < numBoxes; i++) {
              if (boxNumber == boxRelays[i].boxNumber) {
                Serial.print("فتح الصندوق ");
                Serial.println(boxNumber);
                digitalWrite(boxRelays[i].pin, HIGH);
                delay(2000);
                digitalWrite(boxRelays[i].pin, LOW);
                break;
              }
            }
          }
        }
      }
    }
  }
  http.end();
}

void loop() {
  checkUnlockCommands();
  checkRemoteUnlockRequests();

  int currentMicroswitchState = digitalRead(MICROSWITCH_PIN);
  if (currentMicroswitchState != lastMicroswitchState) {
    sendMicroswitchState(currentMicroswitchState);
    lastMicroswitchState = currentMicroswitchState;
  }

  float distanceNow = getDistanceCM();
  int currentDistanceState;

  if (distanceNow <= 3.0) {
    currentDistanceState = 0; // المفتاح غير موجود
  } else if (distanceNow <= 5.0) {
    currentDistanceState = 1; // المفتاح موجود
  } else {
    currentDistanceState = -1; // غير معروف
  }

  if (currentDistanceState != lastDistanceState) {
    lastDistanceState = currentDistanceState;
    sendUltrasonicState(currentDistanceState);
  }

  delay(200);
}
