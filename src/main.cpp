#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ===== WiFi Configuration =====
const char* ssid = "Resticted_Zone_Plus";           
const char* password = "ModalDong26";  

// ===== MQTT Configuration =====
const char* mqtt_server = "f001110f.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_username = "iot_device";
const char* mqtt_password = "prokon123";
const char* mqtt_topic = "iot/temperature";

// WiFi & MQTT Client
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ===== I2C Bus =====
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

// Ultrasonic Pins
#define TRIG_PIN 5
#define ECHO_PIN 18

// LED Pins
#define LED_RED 25
#define LED_GREEN 26

// Filter sederhana
float lastDistance = 50;

// Threshold
#define TEMP_NORMAL 37.5
#define TEMP_LOW 30.0

// State Machine
enum State {
  STATE_IDLE,
  STATE_MEASURE_TEMP,
  STATE_TEMP_HIGH,
  STATE_MOVE_BACK,
  STATE_POSITION_OK,
  STATE_RECOGNIZING,
  STATE_SUCCESS
};

State currentState = STATE_IDLE;
unsigned long stateTimer = 0;
float measuredTemp = 0;  // Dummy temperature
String userName = "Nasyih";

bool rtcAvailable = false;
bool wifiConnected = false;
bool mqttConnected = false;

// Simple & Fast Distance
float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 6000);
  
  if (duration == 0) {
    return lastDistance;
  }
  
  float distance = duration * 0.034 / 2;
  
  if (distance < 1 || distance > 100) {
    return lastDistance;
  }
  
  float smoothed = (distance + lastDistance) / 2.0;
  lastDistance = smoothed;
  
  return smoothed;
}

// DUMMY: Generate random temperature 35-37°C
float getDummyTemp() {
  // Random temp antara 35.0 - 37.0 °C
  float temp = 35.0 + (random(0, 201) / 100.0);  // 35.0 - 37.0
  return temp;
}

void setLED(bool red, bool green) {
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
}

// Connect to WiFi
void connectWiFi() {
  Serial.println("\n--- Connecting to WiFi ---");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connect... ");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi: CONNECTED");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    lcd.setCursor(0, 1);
    lcd.print("WiFi: OK        ");
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi: FAILED");
    lcd.setCursor(0, 1);
    lcd.print("WiFi: FAIL      ");
  }
  
  delay(1000);
}

// MQTT Callback
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT Message: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// Connect to MQTT
void connectMQTT() {
  if (!wifiConnected) return;
  
  // Skip SSL certificate verification (untuk testing)
  espClient.setInsecure();
  
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  Serial.println("\n--- Connecting to MQTT ---");
  
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 3) {
    Serial.print("MQTT connecting... ");
    
    String clientId = "ESP32_" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      mqttConnected = true;
      Serial.println("CONNECTED");
      Serial.print("Client ID: ");
      Serial.println(clientId);
    } else {
      Serial.print("FAILED, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying...");
      attempts++;
      delay(2000);
    }
  }
  
  if (!mqttConnected) {
    Serial.println("MQTT: FAILED after 3 attempts");
  }
}

// Publish data to MQTT
bool publishToMQTT(float temperature) {
  if (!mqttConnected) {
    Serial.println("MQTT: Not connected, skipping publish");
    return false;
  }
  
  // Create JSON payload
  StaticJsonDocument<256> doc;
  doc["temperature"] = temperature;
  doc["status"] = (temperature >= TEMP_NORMAL) ? "high" : "normal";
  doc["device"] = "ESP32_Attendance";
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("\n--- Publishing to MQTT ---");
  Serial.print("Topic: ");
  Serial.println(mqtt_topic);
  Serial.print("Payload: ");
  Serial.println(jsonString);
  
  bool result = mqttClient.publish(mqtt_topic, jsonString.c_str());
  
  if (result) {
    Serial.println("MQTT: Published successfully!");
  } else {
    Serial.println("MQTT: Publish FAILED!");
  }
  
  return result;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== SYSTEM INIT ===");
  
  // Setup hardware
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  setLED(false, false);
  Serial.println("GPIO: OK");
  
  // I2C Bus
  Serial.println("\n--- I2C Bus Init ---");
  Wire.begin();
  Wire.setClock(400000);
  Serial.println("I2C: 400kHz");
  
  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Init System...");
  Serial.println("LCD: OK");
  
  delay(100);
  
  // RTC
  lcd.setCursor(0, 1);
  lcd.print("Init RTC...");
  
  unsigned long rtcStart = millis();
  if (rtc.begin() && (millis() - rtcStart) < 500) {
    rtcAvailable = true;
    Serial.println("RTC: OK");
    
    // UNCOMMENT untuk set waktu WIB
    // rtc.adjust(DateTime(2025, 12, 28, 3, 0, 0));
    
    if (rtc.lostPower()) {
      DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
      DateTime wibTime = DateTime(compileTime.unixtime() + 7 * 3600);
      rtc.adjust(wibTime);
    }
  } else {
    rtcAvailable = false;
    Serial.println("RTC: DISABLED");
  }
  
  delay(200);
  
  // WiFi
  connectWiFi();
  
  // MQTT
  if (wifiConnected) {
    connectMQTT();
  }
  
  // Status akhir
  Serial.println("\n=== INIT COMPLETE ===");
  Serial.print("RTC: ");
  Serial.println(rtcAvailable ? "ACTIVE" : "DISABLED");
  Serial.print("WiFi: ");
  Serial.println(wifiConnected ? "CONNECTED" : "DISCONNECTED");
  Serial.print("MQTT: ");
  Serial.println(mqttConnected ? "CONNECTED" : "DISCONNECTED");
  Serial.println("=====================\n");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  lcd.setCursor(0, 1);
  lcd.print("WiFi:");
  lcd.print(wifiConnected ? "OK " : "-- ");
  lcd.print("MQTT:");
  lcd.print(mqttConnected ? "OK" : "--");
  
  delay(2000);
  lcd.clear();
  
  currentState = STATE_IDLE;
  Serial.println("=== MONITORING START ===\n");
}

void loop() {
  static unsigned long lastRTCRead = 0;
  static unsigned long lastLCDUpdate = 0;
  static unsigned long lastDebug = 0;
  static DateTime cachedTime;
  static char timeStr[12] = "00:00:00";
  
  unsigned long now = millis();
  
  // MQTT Loop (penting untuk maintain connection)
  if (mqttConnected) {
    mqttClient.loop();
  }
  
  // Auto-reconnect MQTT jika terputus
  if (wifiConnected && !mqttClient.connected()) {
    static unsigned long lastReconnect = 0;
    if (now - lastReconnect > 5000) {
      Serial.println("MQTT disconnected, reconnecting...");
      connectMQTT();
      lastReconnect = now;
    }
  }
  
  // Baca distance
  float distance = getDistance();
  
  // Debug setiap 500ms
  if (now - lastDebug >= 500) {
    Serial.print("D:");
    Serial.print(distance, 1);
    Serial.print("cm | S:");
    Serial.println(currentState);
    lastDebug = now;
  }
  
  // RTC - hanya saat IDLE
  if (rtcAvailable && currentState == STATE_IDLE && now - lastRTCRead >= 1000) {
    cachedTime = rtc.now();
    sprintf(timeStr, "%02d:%02d:%02d", cachedTime.hour(), cachedTime.minute(), cachedTime.second());
    lastRTCRead = now;
  }
  
  // LCD throttle
  bool canUpdateLCD = (now - lastLCDUpdate >= 150);
  
  // === STATE MACHINE ===
  
  switch (currentState) {
    
    case STATE_IDLE: {
      setLED(false, false);
      
      if (canUpdateLCD) {
        if (rtcAvailable) {
          lcd.setCursor(0, 0);
          lcd.print(timeStr);
          lcd.print(" WIB    ");
        } else {
          lcd.setCursor(0, 0);
          lcd.print("System Ready    ");
        }
        
        lcd.setCursor(0, 1);
        lcd.print("Dekatkan Dahi   ");
        lastLCDUpdate = now;
      }
      
      if (distance > 0 && distance <= 15) {
        currentState = STATE_MEASURE_TEMP;
        stateTimer = now;
        Serial.println("\n>>> MEASURE_TEMP");
      }
      break;
    }
      
    case STATE_MEASURE_TEMP: {
      
      if (distance >= 3 && distance <= 10) {
        setLED(false, true);
        
        // Generate dummy temperature saat pertama kali measure
        static bool tempGenerated = false;
        if (!tempGenerated) {
          measuredTemp = getDummyTemp();
          tempGenerated = true;
          Serial.print("Dummy Temp Generated: ");
          Serial.print(measuredTemp);
          Serial.println("C");
        }
        
        if (canUpdateLCD) {
          lcd.setCursor(0, 0);
          lcd.print("MENGUKUR SUHU   ");
          lcd.setCursor(0, 1);
          lcd.print("Suhu: ");
          lcd.print(measuredTemp, 1);
          lcd.print("C   ");
          lastLCDUpdate = now;
        }
        
        if (now - stateTimer >= 2000) {
          tempGenerated = false;  // Reset untuk next time
          
          if (measuredTemp >= TEMP_NORMAL) {
            currentState = STATE_TEMP_HIGH;
            Serial.println(">>> TEMP_HIGH");
          } else {
            currentState = STATE_MOVE_BACK;
            Serial.println(">>> MOVE_BACK");
          }
          stateTimer = now;
        }
        
      } else if (distance > 10) {
        setLED(true, false);
        if (canUpdateLCD) {
          lcd.setCursor(0, 0);
          lcd.print("TERLALU JAUH!   ");
          lcd.setCursor(0, 1);
          lcd.print("Dekat: 3-10 cm  ");
          lastLCDUpdate = now;
        }
        
      } else if (distance < 3 && distance > 0) {
        setLED(true, false);
        if (canUpdateLCD) {
          lcd.setCursor(0, 0);
          lcd.print("TERLALU DEKAT!  ");
          lcd.setCursor(0, 1);
          lcd.print("Mundur Sedikit  ");
          lastLCDUpdate = now;
        }
      }
      
      if (now - stateTimer > 15000) {
        currentState = STATE_IDLE;
        lcd.clear();
        Serial.println(">>> IDLE (timeout)");
      }
      break;
    }
      
    case STATE_TEMP_HIGH: {
      setLED(true, false);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("SUHU TINGGI!    ");
        lcd.setCursor(0, 1);
        lcd.print("Suhu: ");
        lcd.print(measuredTemp, 1);
        lcd.print("C   ");
        lastLCDUpdate = now;
      }
      
      digitalWrite(LED_RED, (now / 300) % 2 ? HIGH : LOW);
      
      if (now - stateTimer >= 5000) {
        currentState = STATE_IDLE;
        lcd.clear();
        Serial.println(">>> IDLE");
      }
      break;
    }
      
    case STATE_MOVE_BACK: {
      setLED(true, false);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("MUNDUR 40-50cm! ");
        lcd.setCursor(0, 1);
        lcd.print("Jarak: ");
        
        if (distance > 0 && distance < 100) {
          if (distance < 10) lcd.print(" ");
          lcd.print((int)distance);
          lcd.print(" cm   ");
        } else {
          lcd.print("--- cm  ");
        }
        lastLCDUpdate = now;
      }
      
      if (distance >= 40 && distance <= 50) {
        currentState = STATE_POSITION_OK;
        stateTimer = now;
        Serial.println(">>> POSITION_OK");
      }
      
      if (now - stateTimer > 20000) {
        currentState = STATE_IDLE;
        lcd.clear();
        Serial.println(">>> IDLE (timeout)");
      }
      break;
    }
      
    static bool dataPublished = false;
    case STATE_POSITION_OK: {
      setLED(false, true);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("POSISI OK!      ");
        lcd.setCursor(0, 1);
        lcd.print("Siap scan...    ");
        lastLCDUpdate = now;
      }
      // PUBLISH TO MQTT - Sekali saja saat masuk state POSISI OK
      
      if (!dataPublished) {
        // Get timestamp
        String timestamp;
        if (rtcAvailable) {
          DateTime now_time = rtc.now();
          char buffer[32];
          sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
                  now_time.year(), now_time.month(), now_time.day(),
                  now_time.hour(), now_time.minute(), now_time.second());
          timestamp = String(buffer);
        } else {
          timestamp = String(millis() / 1000) + "s";
        }
        
        // Publish data
        bool published = publishToMQTT(measuredTemp);
        
        if (published) {
          Serial.println("✓ Data sent to EMQX!");
        } else {
          Serial.println("✗ Failed to send data");
        }
        
        dataPublished = true;
      }

       
      
      digitalWrite(LED_GREEN, (now / 200) % 2 ? HIGH : LOW);
      
      if (now - stateTimer >= 1000) {
        currentState = STATE_RECOGNIZING;
        stateTimer = now;
        Serial.println(">>> RECOGNIZING");
      }
      
      if (distance < 40 || distance > 50) {
        currentState = STATE_MOVE_BACK;
        stateTimer = now;
      }
      break;
    }
      
    case STATE_RECOGNIZING: {
      setLED(false, true);
      digitalWrite(LED_GREEN, HIGH);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("Mengenali Wajah ");
        lcd.setCursor(0, 1);
        lcd.print("Tetap Diam...   ");
        lastLCDUpdate = now;
      }
      
      if (distance < 38 || distance > 52) {
        currentState = STATE_MOVE_BACK;
        stateTimer = now;
        break;
      }
      
      if (now - stateTimer >= 5000) {
        currentState = STATE_SUCCESS;
        stateTimer = now;
        Serial.println(">>> SUCCESS!");
      }
      break;
    }
      
    case STATE_SUCCESS: {
      setLED(false, true);
  
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("ABSEN BERHASIL! ");
        lcd.setCursor(0, 1);
        lcd.print("Halo! ");
        lcd.print(userName);
        lcd.print("    ");
        lastLCDUpdate = now;
      }
      
      digitalWrite(LED_GREEN, (now / 500) % 2 ? HIGH : LOW);
      
     if (now - stateTimer >= 5000) {
        currentState = STATE_IDLE;
        measuredTemp = 0;
        dataPublished = false;  // Reset untuk publish berikutnya
        lcd.clear();
        Serial.println(">>> IDLE\n");
      }
      break;
    }
  }
  
  delay(50);
}