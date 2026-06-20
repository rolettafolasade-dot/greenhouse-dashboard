//Hlohonolofatso Rantsane 223028797
/*
 * ============================================================
 *  ESP32 GREENHOUSE MONITOR — MQTT Edition
 *  Broker  : HiveMQ Cloud (TLS port 8883)
 *  Topics  : greenhouse/sensors  (publish)
 *            greenhouse/cmd      (subscribe)
 * ============================================================
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "DHTesp.h"
#include <LiquidCrystal_I2C.h>

// ────────────────────────────────────────────────
// WiFi credentials
// ────────────────────────────────────────────────
const char* ssid     = "SYNTAXSPOT";
const char* password = "20235555";

// ────────────────────────────────────────────────
// HiveMQ Cloud credentials
// ────────────────────────────────────────────────
const char* MQTT_HOST = "757e9f308248406eb752d49b9fde3249.s1.eu.hivemq.cloud"; // ← your cluster URL
const int   MQTT_PORT = 8883;
const char* MQTT_USER = "roletta_esp32";                   // ← your MQTT username
const char* MQTT_PASS = "Roleta2023";              // ← your MQTT password
const char* CLIENT_ID = "greenhouse_esp32";

// ────────────────────────────────────────────────
// MQTT topics
// ────────────────────────────────────────────────
const char* TOPIC_SENSORS  = "greenhouse/sensors";
const char* TOPIC_COMMANDS = "greenhouse/cmd";

WiFiClientSecure espClient;
PubSubClient     mqttClient(espClient);

// ────────────────────────────────────────────────
// PIN DEFINITIONS
// ────────────────────────────────────────────────
#define DHT_PIN           25
#define BUZZER_PIN        26
#define LED_ALERT         32

#define BTN_PUMP          12
#define RELAY_PUMP        14

#define BTN_FAN           27
#define RELAY_FAN         13

#define PIR_PIN           33

#define LDR_SPOTLIGHT     34
#define SOIL_MOISTURE_PIN 35

#define LED_SPOTLIGHT      2

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ────────────────────────────────────────────────
// THRESHOLDS
// ────────────────────────────────────────────────
const float TEMP_HIGH_C         = 30.0;
const float HUMIDITY_HIGH       = 70.0;
const int   SPOTLIGHT_THRESHOLD = 1800;
const int   SOIL_DRY_VALUE      = 3000;
const int   SOIL_WET_VALUE      = 1000;

// ────────────────────────────────────────────────
// BUZZER FREQUENCIES
// ────────────────────────────────────────────────
const int BUZZER_MOTION_HZ = 2300;
const int BUZZER_ENV_HZ    = 1800;

// ────────────────────────────────────────────────
// TIMING INTERVALS
// ────────────────────────────────────────────────
const unsigned long PIR_WARMUP_MS       = 30000UL;
const unsigned long PIR_TIMEOUT_MS      = 4000UL;   // extended: was 2000
const unsigned long DISPLAY_REFRESH_MS  = 1500UL;
const unsigned long SENSOR_READ_INTERVAL= 2000UL;
const unsigned long MQTT_INTERVAL       = 5000UL;
const unsigned long PAGE_FLIP_MS        = 3000UL;
const unsigned long DEBOUNCE_DELAY      = 45UL;
const unsigned long MOTION_CLEAR_DELAY  = 1200UL;   // non-blocking replacement for delay()

// ────────────────────────────────────────────────
// STATE VARIABLES
// ────────────────────────────────────────────────
DHTesp dht;

bool pirReady = false;

// Pump
bool pumpIsOn          = false;
int  lastPumpReading   = HIGH;
int  pumpStable        = HIGH;   // moved to global scope
unsigned long lastPumpDebounce = 0;

// Fan
bool fanIsForcedOn     = false;
bool fanAutoShouldBeOn = false;
int  lastFanReading    = HIGH;
int  fanStable         = HIGH;   // moved to global scope
unsigned long lastFanDebounce = 0;

// Motion
bool motionDetected          = false;
unsigned long lastMotionTime = 0;

// Motion-clear non-blocking timer
bool         motionJustCleared  = false;
unsigned long motionClearedAt   = 0;

// Environment
bool envAlarmActive   = false;
bool spotlightOn      = false;

// Sensors
int   ldrSpotlightValue = 0;
int   soilMoistureRaw   = 0;
int   soilMoisturePct   = 0;
float currentTemp       = 0.0f;
float currentHum        = 0.0f;

// ────────────────────────────────────────────────
// LCD STATE MACHINE
// ────────────────────────────────────────────────
enum DisplayState { NORMAL_A, NORMAL_B, NORMAL_C, MOTION_DETECTED_STATE, NO_MOTION_STATE };
DisplayState currentDisplay = NORMAL_A;

unsigned long lastDisplayUpdate = 0;
unsigned long lastSensorRead    = 0;
unsigned long lastMQTTSend      = 0;
unsigned long lastPageFlip      = 0;

// ────────────────────────────────────────────────
// MQTT: handle incoming commands from dashboard
// ────────────────────────────────────────────────
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String raw;
  for (unsigned int i = 0; i < length; i++) raw += (char)payload[i];

  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, raw)) {
    Serial.println("CMD parse error");
    return;
  }

  const char* cmd = doc["cmd"] | "";

  if (strcmp(cmd, "pump_on")  == 0) { pumpIsOn = true;  digitalWrite(RELAY_PUMP, HIGH); Serial.println("CMD: PUMP ON");  }
  if (strcmp(cmd, "pump_off") == 0) { pumpIsOn = false; digitalWrite(RELAY_PUMP, LOW);  Serial.println("CMD: PUMP OFF"); }
  if (strcmp(cmd, "fan_on")   == 0) { fanIsForcedOn = true;  Serial.println("CMD: FAN ON");  }
  if (strcmp(cmd, "fan_off")  == 0) { fanIsForcedOn = false; Serial.println("CMD: FAN OFF"); }
}

// ────────────────────────────────────────────────
// WiFi connect helper
// ────────────────────────────────────────────────
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  lcd.setCursor(0, 0); lcd.print("Connecting WiFi ");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi → " + WiFi.localIP().toString());
    lcd.setCursor(0, 1); lcd.print("WiFi OK!        ");
  } else {
    Serial.println("\nWiFi FAILED — offline mode");
    lcd.setCursor(0, 1); lcd.print("No WiFi-offline ");
  }
  delay(1500);
  lcd.clear();
}

// ────────────────────────────────────────────────
// MQTT connect helper (also reconnects on drop)
// ────────────────────────────────────────────────
void connectMQTT() {
  espClient.setInsecure();  // accepts any TLS cert; replace with CA cert for production
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setKeepAlive(60);
  mqttClient.setBufferSize(512);

  int mqttAttempts = 0;
  while (!mqttClient.connected() && mqttAttempts < 5) {
    Serial.printf("MQTT connect attempt %d...\n", mqttAttempts + 1);
    if (mqttClient.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
      Serial.println("MQTT connected");
      mqttClient.subscribe(TOPIC_COMMANDS);
    } else {
      Serial.printf("MQTT failed rc=%d\n", mqttClient.state());
      delay(2000);
      mqttAttempts++;
    }
  }
}

// ────────────────────────────────────────────────
// Publish sensor JSON to MQTT broker
// ────────────────────────────────────────────────
void publishSensors() {
  if (!mqttClient.connected()) {
    // Attempt WiFi reconnect before MQTT
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      delay(500);
    }
    connectMQTT();
    if (!mqttClient.connected()) return;  // give up this cycle
  }

  StaticJsonDocument<256> doc;
  doc["temp"]      = currentTemp;
  doc["humidity"]  = currentHum;
  doc["soil_pct"]  = soilMoisturePct;
  doc["ldr"]       = ldrSpotlightValue;
  doc["pump"]      = pumpIsOn      ? 1 : 0;
  doc["fan"]       = (fanIsForcedOn || fanAutoShouldBeOn) ? 1 : 0;
  doc["motion"]    = motionDetected ? 1 : 0;
  doc["alarm"]     = envAlarmActive ? 1 : 0;
  doc["spotlight"] = spotlightOn   ? 1 : 0;

  char buf[256];
  serializeJson(doc, buf);

  if (mqttClient.publish(TOPIC_SENSORS, buf, true)) {
    Serial.println("MQTT → published OK");
  } else {
    Serial.println("MQTT → publish FAILED");
  }
}

// ────────────────────────────────────────────────
// SETUP
// ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\nGreenhouse Monitor (MQTT) starting...\n");

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("Starting...");
  delay(1200);
  lcd.clear();

  dht.setup(DHT_PIN, DHTesp::DHT11);

  pinMode(RELAY_PUMP,    OUTPUT);
  pinMode(RELAY_FAN,     OUTPUT);
  pinMode(LED_ALERT,     OUTPUT);
  pinMode(BUZZER_PIN,    OUTPUT);
  pinMode(LED_SPOTLIGHT, OUTPUT);

  digitalWrite(RELAY_PUMP,    LOW);
  digitalWrite(RELAY_FAN,     LOW);
  digitalWrite(LED_ALERT,     LOW);
  digitalWrite(LED_SPOTLIGHT, LOW);
  noTone(BUZZER_PIN);

  pinMode(BTN_PUMP, INPUT_PULLUP);
  pinMode(BTN_FAN,  INPUT_PULLUP);
  pinMode(PIR_PIN,  INPUT_PULLDOWN);

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    connectMQTT();
  }

  // ── PIR warm-up countdown ──
  Serial.println("PIR warming up (30 s)...");
  unsigned long pirWarmStart = millis();

  while (millis() - pirWarmStart < PIR_WARMUP_MS) {
    unsigned long remaining = (PIR_WARMUP_MS - (millis() - pirWarmStart)) / 1000;
    lcd.setCursor(0, 0); lcd.print("PIR warming up  ");
    lcd.setCursor(0, 1); lcd.print("Ready in: ");
    lcd.print(remaining);
    lcd.print("s   ");
    delay(500);
  }

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("System Ready!   ");
  delay(1000);
  lcd.clear();

  pirReady = true;
  Serial.println("PIR ready. Entering main loop.\n");
}

// ────────────────────────────────────────────────
// LOOP
// ────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Must be called every loop for MQTT to receive messages
  mqttClient.loop();

  // ─────────────── Pump button ───────────────
  int pumpReading = digitalRead(BTN_PUMP);
  if (pumpReading != lastPumpReading) lastPumpDebounce = now;
  if (now - lastPumpDebounce >= DEBOUNCE_DELAY) {
    if (pumpReading != pumpStable) {
      pumpStable = pumpReading;
      if (pumpStable == LOW) {
        pumpIsOn = !pumpIsOn;
        digitalWrite(RELAY_PUMP, pumpIsOn ? HIGH : LOW);
        Serial.println(pumpIsOn ? "PUMP → ON" : "PUMP → OFF");
      }
    }
  }
  lastPumpReading = pumpReading;

  // ─────────────── Fan button ───────────────
  int fanReading = digitalRead(BTN_FAN);
  if (fanReading != lastFanReading) lastFanDebounce = now;
  if (now - lastFanDebounce >= DEBOUNCE_DELAY) {
    if (fanReading != fanStable) {
      fanStable = fanReading;
      if (fanStable == LOW) {
        fanIsForcedOn = !fanIsForcedOn;
        Serial.println(fanIsForcedOn ? "FAN → MANUAL ON" : "FAN → AUTO mode");
      }
    }
  }
  lastFanReading = fanReading;

  // ─────────────── PIR motion detection ───────────────
  if (pirReady && digitalRead(PIR_PIN) == HIGH) {
    motionDetected = true;
    lastMotionTime = now;
  }
  if (motionDetected && (now - lastMotionTime >= PIR_TIMEOUT_MS)) {
    motionDetected     = false;
    motionJustCleared  = true;
    motionClearedAt    = now;
  }

  // ─────────────── Sensor reads every 2 s ───────────────
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    lastSensorRead = now;

    // DHT11
    TempAndHumidity data = dht.getTempAndHumidity();
    if (dht.getStatus() != DHTesp::ERROR_NONE) {
      Serial.println("DHT error: " + String(dht.getStatusString()));
    } else {
      currentTemp = data.temperature;
      currentHum  = data.humidity;
    }

    // Soil moisture
    soilMoistureRaw = analogRead(SOIL_MOISTURE_PIN);
    soilMoisturePct = map(soilMoistureRaw, SOIL_DRY_VALUE, SOIL_WET_VALUE, 0, 100);
    soilMoisturePct = constrain(soilMoisturePct, 0, 100);

    // LDR spotlight
    ldrSpotlightValue = analogRead(LDR_SPOTLIGHT);
    bool shouldBeOn = (ldrSpotlightValue < SPOTLIGHT_THRESHOLD);
    if (shouldBeOn != spotlightOn) {
      spotlightOn = shouldBeOn;
      digitalWrite(LED_SPOTLIGHT, spotlightOn ? HIGH : LOW);
    }

    // Fan + alarm logic
    envAlarmActive    = (currentTemp > TEMP_HIGH_C) || (currentHum > HUMIDITY_HIGH);
    fanAutoShouldBeOn = (currentTemp > TEMP_HIGH_C);
    digitalWrite(RELAY_FAN, (fanIsForcedOn || fanAutoShouldBeOn) ? HIGH : LOW);

    // Serial output
    Serial.println("─────────────────────────────────");
    Serial.printf("Temp    : %.1f °C\n",  currentTemp);
    Serial.printf("Humidity: %.1f %%\n",  currentHum);
    Serial.printf("Soil    : %d raw → %d%%\n", soilMoistureRaw, soilMoisturePct);
    Serial.printf("LDR     : %d  |  Threshold: %d  →  %s\n",
                  ldrSpotlightValue, SPOTLIGHT_THRESHOLD,
                  spotlightOn ? "NIGHT (LED ON)" : "DAY (LED OFF)");
    Serial.printf("Fan     : %s\n", (fanIsForcedOn || fanAutoShouldBeOn) ? "ON" : "OFF");
    Serial.printf("Alarm   : %s\n", envAlarmActive ? "ACTIVE" : "OK");
    Serial.printf("Motion  : %s\n", motionDetected ? "YES" : "No");
  }

  // ─────────────── Buzzer + Alert LED ───────────────
  if (motionDetected) {
    tone(BUZZER_PIN, BUZZER_MOTION_HZ);
    digitalWrite(LED_ALERT, HIGH);
  } else if (envAlarmActive) {
    tone(BUZZER_PIN, BUZZER_ENV_HZ);
    digitalWrite(LED_ALERT, HIGH);
  } else {
    noTone(BUZZER_PIN);
    digitalWrite(LED_ALERT, LOW);
  }

  // ─────────────── LCD Display ───────────────
  if (motionDetected) {
    if (currentDisplay != MOTION_DETECTED_STATE) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("Motion detected");
      lcd.setCursor(0, 1); lcd.print("                ");
      currentDisplay = MOTION_DETECTED_STATE;
    }

  } else {

    // Non-blocking motion-clear message (replaces delay(1200))
    if (motionJustCleared) {
      if (currentDisplay != NO_MOTION_STATE) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("No motion");
        lcd.setCursor(0, 1); lcd.print("detected       ");
        currentDisplay = NO_MOTION_STATE;
      }
      if (now - motionClearedAt >= MOTION_CLEAR_DELAY) {
        motionJustCleared = false;
        lcd.clear();
        currentDisplay    = NORMAL_A;
        lastPageFlip      = now;
        lastDisplayUpdate = now - DISPLAY_REFRESH_MS; // force immediate redraw
      }
      return; // hold this state until timer expires
    }

    // Page flip
    if (now - lastPageFlip >= PAGE_FLIP_MS) {
      lastPageFlip = now;
      if      (currentDisplay == NORMAL_A) currentDisplay = NORMAL_B;
      else if (currentDisplay == NORMAL_B) currentDisplay = NORMAL_C;
      else                                 currentDisplay = NORMAL_A;
      lcd.clear();
      lastDisplayUpdate = now - DISPLAY_REFRESH_MS; // force immediate redraw after clear
    }

    // Refresh current page
    if (now - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
      lastDisplayUpdate = now;

      if (currentDisplay == NORMAL_A) {
        lcd.setCursor(0, 0);
        lcd.print("T:");
        lcd.print(currentTemp, 1);
        lcd.print("C ");
        if (fanIsForcedOn)          lcd.print("Fan:MAN ");
        else if (fanAutoShouldBeOn) lcd.print("Fan:AUTO");
        else                        lcd.print("Fan:OFF ");

        lcd.setCursor(0, 1);
        lcd.print("Soil:");
        lcd.print(soilMoisturePct);
        lcd.print("%");
        if      (soilMoisturePct < 10)  lcd.print("   ");
        else if (soilMoisturePct < 100) lcd.print("  ");
        else                            lcd.print(" ");
        if (envAlarmActive) lcd.print("ALARM!");

      } else if (currentDisplay == NORMAL_B) {
        lcd.setCursor(0, 0);
        lcd.print("H:");
        lcd.print(currentHum, 1);
        lcd.print("%  P:");
        lcd.print(pumpIsOn ? "ON " : "OFF");

        lcd.setCursor(0, 1);
        lcd.print("Soil:");
        lcd.print(soilMoisturePct);
        lcd.print("% ");
        lcd.print(soilMoisturePct < 30 ? "DRY! " : "OK   ");

      } else {  // NORMAL_C
        lcd.setCursor(0, 0);
        lcd.print("Spotlight: ");
        lcd.print(spotlightOn ? "ON  " : "OFF ");

        lcd.setCursor(0, 1);
        lcd.print(spotlightOn ? "Night" : "Day  ");
        lcd.print(" LDR:");
        lcd.print(ldrSpotlightValue);
        lcd.print("    ");
      }
    }
  }

  // ─────────────── MQTT publish every 5 s ───────────────
  if (now - lastMQTTSend >= MQTT_INTERVAL) {
    lastMQTTSend = now;
    publishSensors();
  }
}