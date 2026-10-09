#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>

// -- إعدادات شبكة الواي فاي الحقيقية --
const char* WIFI_SSID = "wifi ";        
const char* WIFI_PASSWORD = "pass"; 

// -- إعدادات منصة ThingSpeak (MQTT v3) --
const char* MQTT_SERVER = "mqtt3..com";
const int   MQTT_PORT = 1883;

// بيانات اعتماد الجهاز (MQTT Device Credentials)
const char* MQTT_CLIENT_ID = "";
const char* MQTT_USERNAME  = "";
const char* MQTT_PASSWORD  = "";

// بيانات القناة والحقول
const long  CHANNEL_ID = ; 
const int   PUBLISH_FIELD = 1;    
const int   SUBSCRIBE_FIELD = 2;  

const int FUEL_SENSOR_PIN = 34; // منفذ حساس مستوى الوقود
const int LED_GREEN_PIN = 18;
const int LED_YELLOW_PIN = 19;
const int LED_RED_PIN = 5;
const float TANK_CAPACITY_L = 10.0;
const float KM_PER_LITER = 5.0;

// قيم المعايرة الحقيقية للعوامة الفعلية 
const int RAW_ADC_FULL = 2090; 
const int RAW_ADC_EMPTY = 280;   

// التوقيت الزمني المضبوط لمنع التضارب مع السحابة (35 ثانية)
const unsigned long PUBLISH_INTERVAL = 35000; 

// مفتاح التشفير السري (Shared Secret Key) - 10 Bytes
const uint8_t SECRET_KEY[10] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
// ==============================================================================
// المتغيرات العامة

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

String publishTopic = "";
String subscribeTopic = "";

unsigned long previousMillis = 0;
float currentLiters = 0.0;
float remainingRangeKm = 0.0;

union Payload64 {
  struct {
    float liters; 
    float range;  
  } values;
  uint8_t bytes[8]; 
};

// جدول PRESENT Cipher S-Box
const uint8_t sBox[16] = {0xC,0x5,0x6,0xB,0x9,0x0,0xA,0xD,0x3,0xE,0xF,0x8,0x4,0x7,0x1,0x2};
// ==============================================================================
// دوال النظام

//دالة P-layer
void presentPLayer(uint8_t* state) {
  // مصفوفة مؤقتة
  uint8_t temp[8] = {0};
  for (int i = 0; i < 64; i++) {
    // استخراج البت
    int srcByte = i / 8;
    int srcBit  = 7 - (i % 8);
    uint8_t bit = (state[srcByte] >> srcBit) & 0x01;
    // حساب المكان الجديد للبت
    int destination; 
    if (i == 63) {
      destination = 63;
    }
    else {
      destination = (16 * i) % 63;
    }
    int dstByte = destination / 8;
    int dstBit  = 7 - (destination % 8);
    // وضع البت في مكانه الجديد
    temp[dstByte] |= (bit << dstBit);
  }
  // نسخ النتيجة الجديدةالى مكانها
  for (int i = 0; i < 8; i++) {
    state[i] = temp[i];
  }
}
// دالة التشفير
void presentEncrypt(uint8_t* state, const uint8_t* key) {
  for(int round=1; round<=31; round++) {
   
   // AddRoundKey
    for(int i=0; i<8; i++) {
      state[i] ^= key[i]; 
    }
   
   // S-box
    for(int i=0; i<8; i++) {
      uint8_t high = sBox[state[i] >> 4];
      uint8_t low = sBox[state[i] & 0x0F];
      state[i] = (high << 4) | low; 
    }
   
   // P-layer
    presentPLayer(state);
  }
}
// دالة الاستقبال من السحابة
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.println("\n--- [استقبال بيانات من السحابة (MQTT Receive)] ---");
  Serial.print("Topic: ");
  Serial.println(topic);
  
  String receivedData = "";
  for (int i = 0; i < length; i++) {
    receivedData += (char)payload[i];
  }
  
  Serial.print("Cloud Reaction (Decrypted Liters): ");
  Serial.println(receivedData);
  
  float cloudLiters = receivedData.toFloat();
  if (cloudLiters < 2.0) {
    Serial.println("Action Response: Cloud confirms CRITICAL LOW FUEL!");
  } else {
    Serial.println("Action Response: Cloud confirms SAFE level.");
  }
  Serial.println("--------------------------------------------------\n");
}
// دالة الاتصال بالواي فاي
void setupWiFi() {
  Serial.print("Connecting to WiFi Network: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[نجاح] تم الاتصال بشبكة WiFi بنجاح!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}
// دالة الاتصال بـ MQTT
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection to ThingSpeak...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("[نجاح] تم الاتصال بمنصة ThingSpeak (MQTT) بنجاح!");
      mqttClient.subscribe(subscribeTopic.c_str());
      Serial.print("[نجاح] تم الاشتراك في المسار: ");
      Serial.println(subscribeTopic);
    } else {
      Serial.print("[فشل] خطأ في الاتصال، الكود: ");
      Serial.print(mqttClient.state());
      Serial.println(" - إعادة المحاولة بعد 5 ثواني...");
      delay(5000);
    }
  }
}
// معادلة التحويل الخطي
float float_map(long x, long in_min, long in_max, float out_min, float out_max) {
  return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== بدء تشغيل النظام الذكي لمراقبة الوقود ===");
  publishTopic = "channels/" + String(CHANNEL_ID) + "/publish";
  subscribeTopic = "channels/" + String(CHANNEL_ID) + "/subscribe/fields/field" + String(SUBSCRIBE_FIELD);

  lcd.init();
  lcd.backlight();
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_YELLOW_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);

  setupWiFi();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback); 
  
  lcd.setCursor(0, 0); lcd.print("System Ready!");
  delay(2000); lcd.clear();
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop(); 

  unsigned long currentMillis = millis();

  // تنفيذ الكود كل 35 ثانية
  if (currentMillis - previousMillis >= PUBLISH_INTERVAL) {
    previousMillis = currentMillis;

    Serial.println("\n================= دورة قراءة جديدة (35 ثانية) =================");
    
    // فلتر تنعيم الضجيج التناظري (متوسط 20 قراءة)
    long sumADC = 0;
    for (int i = 0; i < 20; i++) {
      sumADC += analogRead(FUEL_SENSOR_PIN);
      delay(10);
    }
    int rawADC = sumADC / 20;

    // حساب اللترات مع ضمان عدم تجاوز الحدود
    currentLiters = float_map(
      rawADC,
      RAW_ADC_EMPTY,
      RAW_ADC_FULL,
      0.0,
      TANK_CAPACITY_L
    );

    if (currentLiters > TANK_CAPACITY_L)
      currentLiters = TANK_CAPACITY_L;

    if (currentLiters < 0.0)
      currentLiters = 0.0;

    remainingRangeKm = currentLiters * KM_PER_LITER;

    // تحديث الشاشة واللمبات
    lcd.setCursor(0, 0);

    if (currentLiters < 2.0) {

      lcd.print("Engine Stopped! ");

      digitalWrite(LED_GREEN_PIN, LOW);
      digitalWrite(LED_YELLOW_PIN, LOW);
      digitalWrite(LED_RED_PIN, HIGH);

    } else {

      lcd.print("Fuel: ");
      lcd.print(currentLiters, 1);
      lcd.print(" L   ");

      if (currentLiters >= 7.0) {

        digitalWrite(LED_GREEN_PIN, HIGH);
        digitalWrite(LED_YELLOW_PIN, LOW);
        digitalWrite(LED_RED_PIN, LOW);

      } else {

        digitalWrite(LED_GREEN_PIN, LOW);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        digitalWrite(LED_RED_PIN, LOW);
      }
    }

    lcd.setCursor(0, 1);
    lcd.print("Range: ");
    lcd.print(remainingRangeKm, 1);
    lcd.print(" km  ");


    // تشفير البيانات
    Serial.println("--- [تشفير البيانات (Encryption)] ---");

    Payload64 payload;

    payload.values.liters = currentLiters;
    payload.values.range = remainingRangeKm;
    
    Serial.print("Clear Data -> Fuel: ");
    Serial.print(currentLiters); 

    Serial.print(" L, Range: ");
    Serial.print(remainingRangeKm);
    Serial.println(" km");

    presentEncrypt(payload.bytes, SECRET_KEY); 


    // تحويل البيانات لـ Hex
    String encryptedHex = "";

    for (int i = 0; i < 8; i++) {

      if (payload.bytes[i] < 16)
        encryptedHex += "0";

      encryptedHex += String(payload.bytes[i], HEX);
    }

    encryptedHex.toUpperCase();
    Serial.print("Encrypted Hex String: ");
    Serial.println(encryptedHex);


    // الإرسال للسحابة
    Serial.println("--- [إرسال البيانات (MQTT Publish)] ---");
    String mqttPayload = "field1=" + encryptedHex + "&field4=1";
    Serial.print("Publishing to Topic: ");
    Serial.println(publishTopic);
    Serial.print("Payload: ");
    Serial.println(mqttPayload);
    
    if (mqttClient.publish(
          publishTopic.c_str(),
          mqttPayload.c_str()
        )) {

      Serial.println("[نجاح] تم إرسال البيانات المشفرة إلى المنصة بنجاح!");

    } else {

      Serial.println("[فشل] لم يتم إرسال البيانات.");
    }

    Serial.println("===============================================================\n");
  }
}