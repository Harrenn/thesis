#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <time.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define SENSOR  4  
#define BLYNK_PRINT Serial
#define WIFI_SSID "HUAWEI-4049"
#define WIFI_PASS "personalwifisss1"

const char* mqtt_server = "20.163.192.238";
const int mqtt_port = 1883;

char auth[] = "_rT-A_f1jKTxycyowJ-kkFkVaieceJN2"; 

volatile byte pulseCount;
byte pulse1Sec = 0;
float calibrationFactor = 6.9;
float flowMilliLitresPerSecond;
float totalCubicMeter = 0.0;
unsigned long previousMillis = 0;
int batteryLevel = 100;
unsigned long lastSensorCheck = 0;
const unsigned long sensorCheckInterval = 5000; 
bool dataFromSensor = false;
BlynkTimer timer;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// Buffer for the simple moving average
#define BUFF_SIZE 50
float buff[BUFF_SIZE];
int buffIndex = 0;
bool bufferFull = false;

void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void sendBatteryLevel() {
  Blynk.virtualWrite(V0, batteryLevel);
}

void decreaseBatteryLevel() {
  if (batteryLevel > 0) {
    batteryLevel--;
  }
}

void setup_mqtt() {
  mqttClient.setServer(mqtt_server, mqtt_port);
  while (!mqttClient.connected()) {
    if (mqttClient.connect("ESP32Client")) {
    } else {
      delay(2000);
    }
  }
}

BLYNK_WRITE(V33) {
  String input_text = param.asStr(); 
  Blynk.virtualWrite(V30, 0.0); 
  delay(1000);
  ESP.restart();
}

void setup() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    ESP.restart();
  }
  
  ArduinoOTA.begin();
  Blynk.begin(auth, WIFI_SSID, WIFI_PASS);
  setup_mqtt();
  
  Blynk.syncVirtual(V30);
  Blynk.syncVirtual(V0);

  pinMode(SENSOR, INPUT_PULLUP);
  pulseCount = 0;
  flowMilliLitresPerSecond = 0.0;
  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);
  
  configTime(8 * 3600, 0, "pool.ntp.org");

  struct tm timeinfo;

  timer.setInterval(1000L, sendBatteryLevel);
  timer.setInterval(300000L, decreaseBatteryLevel);
  timer.setInterval(5000L, checkSensor);  // Check sensor status every 5 seconds

  // Initialize the buffer for the moving average
  for(int i = 0; i < BUFF_SIZE; i++)
    buff[i] = 0;
}

BLYNK_WRITE(V30) {
  totalCubicMeter = param.asFloat(); 
}

BLYNK_WRITE(V0) {
  batteryLevel = param.asInt(); 
}

// Calculate the simple moving average
float movingAvgFlowRate() {
  float sum = 0;
  for(int i = 0; i < BUFF_SIZE; i++)
    sum += buff[i];
  
  return sum / ((bufferFull) ? BUFF_SIZE : buffIndex);
}

void calculateFlow() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > 1000) {
    pulse1Sec = pulseCount;
    pulseCount = 0;
    float flowRateLperSec = ((1000.0 / (currentMillis - previousMillis)) * pulse1Sec) / calibrationFactor;
    flowMilliLitresPerSecond = (flowRateLperSec * 1000)/60;

    // Add the flow rate to the buffer
    buff[buffIndex] = flowMilliLitresPerSecond;
    buffIndex = (buffIndex + 1) % BUFF_SIZE;
    if(buffIndex == 0) bufferFull = true;
    float smoothedFlowRate = movingAvgFlowRate();

    totalCubicMeter += smoothedFlowRate / (1000 * 1000 * 60);
    previousMillis = currentMillis;
    Blynk.virtualWrite(V27, smoothedFlowRate);
    Blynk.virtualWrite(V30, totalCubicMeter);
    
    mqttClient.publish("flowRate", String(smoothedFlowRate, 2).c_str());
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      if (timeinfo.tm_mday == 1 && previousMillis != 1) {
        totalCubicMeter = 0.0;
        Blynk.virtualWrite(V30, totalCubicMeter);
      }
    }
  }
}

void checkSensor() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastSensorCheck > sensorCheckInterval) {
    dataFromSensor = pulseCount > 0;
    if (dataFromSensor) {
      Blynk.virtualWrite(V37, "Online");
    } else {
      Blynk.virtualWrite(V37, "Offline");
    }
    lastSensorCheck = currentMillis;
  }
}

void loop() {
  Blynk.run();
  timer.run();
  calculateFlow();
  ArduinoOTA.handle();
  if (!mqttClient.connected()) {
    setup_mqtt();
  }
  mqttClient.loop();
}
