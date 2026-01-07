#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient. h>
#include <ArduinoJson.h>
#include <Adafruit_MLX90614.h>

// ===== WiFi Configuration =====
const char* ssid = "Rifki";           
const char* password = "hhhhhhhh";  

// ===== MQTT Configuration (COMMENTED) =====
const char* mqtt_server = "f001110f.ala.asia-southeast1.emqxsl.com";
const int mqtt_port = 8883;
const char* mqtt_username = "iot_device";
const char* mqtt_password = "prokon123";
const char* mqtt_topic = "prokon_device/data";

// WiFi Client
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// ===== I2C Bus =====
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
Adafruit_MLX90614 mlx = Adafruit_MLX90614();

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
#define TEMP_OFFSET 0.0

// MLX90614 I2C Address
#define MLX90614_I2C_ADDR 0x5A

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
float measuredTemp = 0;
String userName = "Nasyih";

bool rtcAvailable = false;
bool wifiConnected = false;
// bool mqttConnected = false;
bool mlxAvailable = false;

// ===== I2C SCANNER =====
void scanI2C() {
  Serial.println("\n=== I2C Device Scanner ===");
  byte error, address;
  int nDevices = 0;

  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      // Identify known devices
      if (address == 0x27) Serial.print(" (LCD)");
      if (address == 0x68) Serial.print(" (RTC)");
      if (address == 0x5A) Serial.print(" (MLX90614)");
      
      Serial.println();
      nDevices++;
    }
  }

  if (nDevices == 0) {
    Serial.println("No I2C devices found!");
  } else {
    Serial.print("Total devices found: ");
    Serial.println(nDevices);
  }
  Serial.println("========================\n");
}

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

// ===== TEST MLX90614 =====
bool testMLX90614() {
  Serial.println("\n--- Testing MLX90614 (Proven Method) ---");
  
  // Test 1: Check I2C Communication
  Serial.print("Step 1: Pinging 0x5A...  ");
  Wire.beginTransmission(MLX90614_I2C_ADDR);
  byte error = Wire.endTransmission();
  
  if (error != 0) {
    Serial.println("✗ NO RESPONSE!");
    Serial.println("MLX90614 not found at 0x5A");
    return false;
  }
  Serial.println("✓ ACK received!");
  
  delay(200);
  
  // Test 2: Try library init
  Serial.print("Step 2: Library init... ");
  if (!mlx.begin()) {
    Serial.println("✗ FAILED!");
    return false;
  }
  Serial.println("✓ SUCCESS!");
  
  delay(200);
  
  // Test 3: Read temperature (5 samples)
  Serial.println("Step 3: Reading samples.. .");
  
  int validSamples = 0;
  for (int i = 1; i <= 5; i++) {
    float ambient = mlx.readAmbientTempC();
    float object = mlx.readObjectTempC();
    
    Serial.print("  Sample ");
    Serial.print(i);
    Serial.print(": Ambient=");
    Serial.print(ambient, 2);
    Serial.print("°C | Object=");
    Serial.print(object, 2);
    Serial.print("°C");
    
    if (! isnan(ambient) && !isnan(object) && 
        ambient >= -40 && ambient <= 125 && 
        object >= -70 && object <= 380) {
      Serial.println(" ✓");
      validSamples++;
    } else {
      Serial. println(" ✗");
    }
    
    delay(500);
  }
  
  Serial.print("Valid samples: ");
  Serial.print(validSamples);
  Serial.println("/5");
  
  if (validSamples >= 3) {
    Serial.println("✓✓✓ MLX90614 FULLY OPERATIONAL ✓✓✓");
    Serial.println("----------------------------------------");
    return true;
  } else {
    Serial.println("✗ Insufficient valid readings");
    Serial.println("Sensor may be faulty");
    return false;
  }
}

// ===== BACA SUHU REAL =====
float getRealTemp() {
  if (!mlxAvailable) {
    Serial.println("MLX90614: Not available!");
    return 0;
  }
  
  const int samples = 5;
  float sum = 0;
  int validCount = 0;
  
  Serial.println("\n--- Temperature Reading ---");
  
  for (int i = 0; i < samples; i++) {
    delay(100);
    
    float temp = mlx.readObjectTempC();
    
    Serial.print("Sample ");
    Serial.print(i+1);
    Serial.print(": ");
    
    if (isnan(temp)) {
      Serial.println("NaN");
      continue;
    }
    
    // Validasi range untuk manusia (25-45°C)
    if (temp >= 25.0 && temp <= 45.0) {
      temp += TEMP_OFFSET;
      sum += temp;
      validCount++;
      Serial.print(temp, 2);
      Serial.println("°C ✓");
    } else {
      Serial.print(temp, 2);
      Serial.println("°C (out of range)");
    }
  }
  
  if (validCount == 0) {
    Serial.println("ERROR: No valid readings!");
    return 0;
  }
  
  float avgTemp = sum / validCount;
  Serial.print("Valid samples: ");
  Serial.print(validCount);
  Serial.print("/");
  Serial.println(samples);
  Serial.print("Average Temperature: ");
  Serial.print(avgTemp, 2);
  Serial.println("°C");
  Serial.println("---------------------------");
  
  return avgTemp;
}

// Baca suhu ambient
float getAmbientTemp() {
  if (!mlxAvailable) return 0;
  
  float temp = mlx.readAmbientTempC();
  
  if (isnan(temp)) {
    return 0;
  }
  
  return temp + TEMP_OFFSET;
}

void setLED(bool red, bool green) {
  digitalWrite(LED_RED, red ?  HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH :  LOW);
}

// ===== DISPLAY DATA KE LCD =====
void displayDataToLCD(float temperature) {
  Serial.println("\n--- Displaying Data to LCD ---");
  
  // Clear LCD
  lcd.clear();
  
  // Line 1: Temperature
  lcd.setCursor(0, 0);
  lcd.print("Temp: ");
  lcd.print(temperature, 1);
  lcd.print("C");
  
  // Line 2: Status & User
  lcd.setCursor(0, 1);
  if (temperature >= TEMP_NORMAL) {
    lcd.print("Status: HIGH");
  } else {
    lcd.print("User: ");
    lcd.print(userName);
  }
  
  // Print to Serial
  Serial.print("Temperature: ");
  Serial.print(temperature, 2);
  Serial.println("°C");
  Serial.print("Status: ");
  Serial.println(temperature >= TEMP_NORMAL ? "HIGH" : "NORMAL");
  Serial.print("User: ");
  Serial.println(userName);
  
  // Timestamp jika RTC tersedia
  if (rtcAvailable) {
    DateTime now_time = rtc.now();
    char timestamp[32];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
            now_time. year(), now_time.month(), now_time.day(),
            now_time.hour(), now_time.minute(), now_time.second());
    Serial.print("Timestamp: ");
    Serial.println(timestamp);
  }
  
  Serial.println("-----------------------------");
}

// Connect to WiFi
void connectWiFi() {
  Serial.println("\n--- Connecting to WiFi ---");
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connect...  ");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi. status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi:  CONNECTED");
    Serial.print("IP:  ");
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

// ===== MQTT Callback (COMMENTED) =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT Message: ");
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// ===== Connect to MQTT (COMMENTED) =====
void connectMQTT() {
  if (! wifiConnected) return;
  
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
      Serial. print("FAILED, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying...");
      attempts++;
      delay(2000);
    }
  }
  
  if (!mqttConnected) {
    Serial.println("MQTT:  FAILED after 3 attempts");
  }
}

// ===== Publish to MQTT (COMMENTED) =====
bool publishToMQTT(float temperature) {
  if (!mqttConnected) {
    Serial.println("MQTT: Not connected, skipping publish");
    return false;
  }
  
  StaticJsonDocument<256> doc;
  doc["temperature"] = temperature;
  doc["status"] = (temperature >= TEMP_NORMAL) ? "high" : "normal";
  doc["device"] = "ESP32_Attendance";
  doc["user"] = userName;
  
  if (rtcAvailable) {
    DateTime now_time = rtc.now();
    char timestamp[32];
    sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
            now_time.year(), now_time.month(), now_time.day(),
            now_time.hour(), now_time.minute(), now_time.second());
    doc["timestamp"] = timestamp;
  }
  
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
    Serial.println("MQTT:  Publish FAILED!");
  }
  
  return result;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== SYSTEM INIT ===");
  
  // Setup hardware
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  setLED(false, false);
  Serial.println("GPIO:  OK");
  
  // ===== I2C Bus Init =====
  Serial.println("\n--- I2C Bus Init ---");
  Wire.begin(21, 22);  // SDA=21, SCL=22
  Wire.setClock(100000);  // 100kHz
  Serial.println("I2C: 100kHz on SDA=21, SCL=22");
  delay(100);
  
  // Scan I2C devices
  scanI2C();
  
  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Init System...");
  Serial.println("LCD: OK");
  
  delay(500);
  
  // RTC
  lcd.setCursor(0, 1);
  lcd.print("Init RTC...");
  
  unsigned long rtcStart = millis();
  if (rtc.begin() && (millis() - rtcStart) < 500) {
    rtcAvailable = true;
    Serial.println("RTC: OK");
    
    if (rtc.lostPower()) {
      DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
      DateTime wibTime = DateTime(compileTime.unixtime() + 7 * 3600);
      rtc.adjust(wibTime);
    }
  } else {
    rtcAvailable = false;
    Serial.println("RTC:  DISABLED");
  }
  
  delay(500);
  
  // ===== MLX90614 INIT =====
  lcd.setCursor(0, 1);
  lcd.print("Init MLX90614...");
  
  Serial.println("\n================================");
  Serial.println("   MLX90614 INITIALIZATION");
  Serial.println("================================");
  
  if (testMLX90614()) {
    mlxAvailable = true;
    Serial.println("\n✓✓✓ MLX90614:  READY ✓✓✓\n");
    lcd.setCursor(0, 1);
    lcd.print("MLX90614: OK    ");
  } else {
    mlxAvailable = false;
    Serial.println("\n✗✗✗ MLX90614: FAILED ✗✗✗");
    Serial.println("System will continue with limited functionality\n");
    lcd.setCursor(0, 1);
    lcd.print("MLX90614: FAIL  ");
    
    // Warning blink
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_RED, HIGH);
      delay(200);
      digitalWrite(LED_RED, LOW);
      delay(200);
    }
  }
  
  delay(1000);
  
  // WiFi
  connectWiFi();
  
  // ===== MQTT (COMMENTED) =====
  if (wifiConnected) {
    connectMQTT();
  }
  
  // Status akhir
  Serial.println("\n=== INIT COMPLETE ===");
  Serial.print("RTC: ");
  Serial.println(rtcAvailable ?  "ACTIVE" : "DISABLED");
  Serial.print("MLX90614: ");
  Serial.println(mlxAvailable ?  "ACTIVE" : "DISABLED");
  Serial.print("WiFi: ");
  Serial.println(wifiConnected ?  "CONNECTED" : "DISCONNECTED");
  Serial.print("MQTT: ");
  Serial.println(mqttConnected ? "CONNECTED" : "DISCONNECTED");
  Serial.println("=====================\n");
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Ready!");
  lcd.setCursor(0, 1);
  lcd.print("MLX:");
  lcd.print(mlxAvailable ? "OK " : "!!  ");
  lcd.print("WiFi:");
  lcd.print(wifiConnected ? "OK" : "--");
  lcd.print("MQTT:");
  lcd.print(mqttConnected ? "OK" : "--");
  
  delay(3000);
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
  static bool dataDisplayed = false;
  
  unsigned long now = millis();
  
  // ===== MQTT Loop (COMMENTED) =====
  if (mqttConnected) {
    mqttClient.loop();
  }
  
  // ===== Auto-reconnect MQTT (COMMENTED) =====
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
  
  // Debug setiap 1 detik
  if (now - lastDebug >= 1000) {
    Serial.print("D:");
    Serial.print(distance, 1);
    Serial.print("cm | S:");
    Serial.print(currentState);
    
    // Ambient temp monitoring
    if (mlxAvailable && currentState == STATE_IDLE) {
      float ambient = getAmbientTemp();
      Serial.print(" | Ambient:");
      if (ambient > 0) {
        Serial.print(ambient, 1);
        Serial.print("°C");
      } else {
        Serial.print("ERR");
      }
    }
    
    Serial.println();
    lastDebug = now;
  }
  
  // RTC
  if (rtcAvailable && currentState == STATE_IDLE && now - lastRTCRead >= 1000) {
    cachedTime = rtc.now();
    sprintf(timeStr, "%02d:%02d:%02d", cachedTime.hour(), cachedTime.minute(), cachedTime.second());
    lastRTCRead = now;
  }
  
  // LCD throttle
  bool canUpdateLCD = (now - lastLCDUpdate >= 150);
  
  // === STATE MACHINE ===
  
  switch (currentState) {
    
    case STATE_IDLE:  {
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
        if (mlxAvailable) {
          lcd.print("Dekatkan Dahi   ");
        } else {
          lcd.print("MLX ERROR!       ");
        }
        lastLCDUpdate = now;
      }
      
      if (distance > 0 && distance <= 15 && mlxAvailable) {
        currentState = STATE_MEASURE_TEMP;
        stateTimer = now;
        Serial.println("\n>>> MEASURE_TEMP");
      }
      break;
    }
      
    case STATE_MEASURE_TEMP: {
      
      if (distance >= 15 && distance <= 20) {
        setLED(false,true);
        
        static bool tempRead = false;
        if (!tempRead) {
          measuredTemp = getRealTemp();
          tempRead = true;
          
          if (measuredTemp > 0) {
            Serial.print("✓ Temperature Measured: ");
            Serial.print(measuredTemp, 2);
            Serial.println("°C");
          } else {
            Serial.println("✗ Failed to read temperature!");
          }
        }
        
        if (canUpdateLCD) {
          lcd.setCursor(0, 0);
          lcd.print("MENGUKUR SUHU   ");
          lcd.setCursor(0, 1);
          
          if (measuredTemp > 0) {
            lcd.print("Suhu: ");
            lcd.print(measuredTemp, 1);
            lcd.print("C   ");
          } else {
            lcd.print("Sensor Error!    ");
          }
          
          lastLCDUpdate = now;
        }
        
        if (now - stateTimer >= 3000) {
          tempRead = false;
          
          if (measuredTemp == 0) {
            currentState = STATE_IDLE;
            lcd.clear();
            Serial.println(">>> IDLE (sensor error)");
          } else if (measuredTemp >= TEMP_NORMAL) {
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
          lcd.print("TERLALU JAUH!    ");
          lcd.setCursor(0, 1);
          lcd.print("Dekat:  3-10 cm  ");
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
      
    case STATE_TEMP_HIGH:  {
      setLED(true, false);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("SUHU TINGGI!     ");
        lcd.setCursor(0, 1);
        lcd.print("Suhu: ");
        lcd.print(measuredTemp, 1);
        lcd.print("C   ");
        lastLCDUpdate = now;
      }
      
      digitalWrite(LED_RED, (now / 300) % 2 ? HIGH : LOW);
      
      if (now - stateTimer >= 5000) {
        currentState = STATE_IDLE;
        measuredTemp = 0;
        lcd.clear();
        Serial.println(">>> IDLE");
      }
      break;
    }
      
    case STATE_MOVE_BACK: {
      setLED(true, false);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("MUNDUR 40-50cm!  ");
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
        measuredTemp = 0;
        lcd.clear();
        Serial.println(">>> IDLE (timeout)");
      }
      break;
    }
      
    case STATE_POSITION_OK: {
      setLED(false,true);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("POSISI OK!      ");
        lcd.setCursor(0, 1);
        lcd.print("Siap scan...     ");
        lastLCDUpdate = now;
      }
      
      // ===== DISPLAY DATA KE LCD =====
      if (! dataDisplayed) {
        displayDataToLCD(measuredTemp);
        dataDisplayed = true;
        
        // Tampilkan di LCD selama 3 detik
        delay(3000);
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
      setLED(false,true);
      digitalWrite(LED_GREEN, HIGH);
      
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("Mengenali Wajah ");
        lcd.setCursor(0, 1);
        lcd.print("Tetap Diam...    ");
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
      setLED(false,true);
  
      if (canUpdateLCD) {
        lcd.setCursor(0, 0);
        lcd.print("ABSEN BERHASIL! ");
        lcd.setCursor(0, 1);
        lcd.print("Halo!  ");
        lcd.print(userName);
        lcd.print("    ");
        lastLCDUpdate = now;
      }
      
      digitalWrite(LED_GREEN, (now / 500) % 2 ? HIGH : LOW);
      
      if (now - stateTimer >= 5000) {
        currentState = STATE_IDLE;
        measuredTemp = 0;
        dataDisplayed = false;
        lcd.clear();
        Serial.println(">>> IDLE\n");
      }
      break;
    }
  }
  
  delay(50);
}