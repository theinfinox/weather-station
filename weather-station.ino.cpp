# 1 "C:\\Users\\1sree\\AppData\\Local\\Temp\\tmpwhbchz7n"
#include <Arduino.h>
# 1 "C:/Users/1sree/Desktop/newhss-tech/weather/weather-station/weather-station.ino"
# 13 "C:/Users/1sree/Desktop/newhss-tech/weather/weather-station/weather-station.ino"
#include <Arduino.h>


struct WiFiCredential {
    const char* ssid;
    const char* password;
};

WiFiCredential networks[] = {
    {"esp", "esp12345"},
    {"pi", "raspberry"}
};
const int numNetworks = sizeof(networks) / sizeof(networks[0]);



const char* vercelUrl = "https://weather-station-esp.vercel.app/api/update";

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include "RobotEyes.h"


void handleRoot();
void handleJson();
void drawWifiIcon(int x, int y);
void drawFace();
void drawDiagnostics();
void syncWithVercel();
void readSensors();
void checkButton();


#define DHTPIN D4
#define DHTTYPE DHT11
#define BUTTON_PIN D5
#define MOISTURE_PIN A0
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64



const int DISCONNECTED_THRESHOLD = 35;
const int DRY_VAL = 850;
const int WET_VAL = 350;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
RobotEyes eyes;


float t = 24.0, h = 60.0;
int rawMoist = 0;
int moisture = -1;
bool probeConnected = false;
String mood = "searching";
String statusMsg = "Sensor Not Connected ⚠️";
int petCount = 0;
bool mdnsStarted = false;


enum DisplayMode { MODE_FACE, MODE_DIAGNOSTIC };
DisplayMode currentMode = MODE_FACE;
unsigned long heartStartTime = 0;
bool showHeart = false;
bool pendingSync = false;
EyeEmotion currentEmotion = EMOTION_NEUTRAL;


unsigned long lastAnimTime = 0;
unsigned long lastSensorTime = 0;
unsigned long lastVercelSync = -60000;


bool lastButtonState = HIGH;
unsigned long buttonPressStart = 0;
bool longPressTriggered = false;


BearSSL::WiFiClientSecure* secureClient = nullptr;

const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Flori - Local Plant Companion</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
  body{font-family:sans-serif;background:#FAFAFA;color:#111111;text-align:center;padding:16px;margin:0}
  .card{max-width:540px;margin:0 auto;background:#FFFFFF;padding:20px;border-radius:16px;border:1px solid #EAEAEA;box-shadow:0 4px 6px -1px rgba(0,0,0,0.05)}
  .pills{display:flex;justify-content:center;gap:12px;margin:16px 0;flex-wrap:wrap}
  .pill{padding:8px 14px;border-radius:999px;background:#F7F7F7;border:1px solid #F2F2F2;font-size:14px;color:#333333}
  .val{font-size:18px;font-weight:bold;color:#111111}
  canvas{width:100%!important;max-height:260px}
</style>
</head><body>
<div class='card'>
  <h2 style='color:#111111;margin-top:0;'>🌱 Flori Local Monitor</h2>
  <p id='moodText' style='color:#666666'>Evaluating...</p>
  <div class='pills'>
    <div class='pill'>Moisture: <span id='m' class='val'>--</span></div>
    <div class='pill'>Temp: <span id='t' class='val'>--</span>°C</div>
    <div class='pill'>Hum: <span id='h' class='val'>--</span>%</div>
  </div>
  <canvas id='chart'></canvas>
</div>
<script>
const ws = new WebSocket('ws://' + location.hostname + ':81/');
let chart = new Chart(document.getElementById('chart').getContext('2d'), {
  type:'line',
  data:{labels:[],datasets:[
    {label:'Soil Moist %',borderColor:'#111111',backgroundColor:'#111111',data:[],yAxisID:'y1'},
    {label:'Temp °C',borderColor:'#666666',backgroundColor:'#666666',data:[],yAxisID:'y2'},
    {label:'Hum %',borderColor:'#333333',backgroundColor:'#333333',data:[],yAxisID:'y1'}
  ]},
  options:{animation:false,scales:{y1:{type:'linear',position:'left',min:0,max:100},y2:{type:'linear',position:'right'}}}
});
ws.onmessage = e=>{
  let d = JSON.parse(e.data);
  document.getElementById('m').textContent = (d.connected === false || d.m < 0) ? 'Not Connected' : d.m + '%';
  document.getElementById('t').textContent = d.t.toFixed(1);
  document.getElementById('h').textContent = d.h.toFixed(1);
  document.getElementById('moodText').textContent = 'Status: ' + d.status;
  let time = new Date().toLocaleTimeString();
  chart.data.labels.push(time);
  chart.data.datasets[0].data.push(d.m >= 0 ? d.m : null);
  chart.data.datasets[1].data.push(d.t);
  chart.data.datasets[2].data.push(d.h);
  if(chart.data.labels.length>25){
    chart.data.labels.shift();
    chart.data.datasets.forEach(ds=>ds.data.shift());
  }
  chart.update();
};
</script></body></html>
)rawliteral";


void handleRoot() {
  server.sendHeader("Content-Type", "text/html; charset=utf-8");
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleJson() {
  String json = "{\"t\":" + String(t,1) +
                ",\"h\":" + String(h,1) +
                ",\"m\":" + String(moisture) +
                ",\"connected\":" + (probeConnected ? "true" : "false") +
                ",\"raw\":" + String(rawMoist) +
                ",\"mood\":\"" + mood + "\"" +
                ",\"status\":\"" + statusMsg + "\"" +
                ",\"pets\":" + String(petCount) + "}";
  server.send(200, "application/json", json);
}


void drawWifiIcon(int x, int y) {
  bool online = (WiFi.status() == WL_CONNECTED);
  if (!online) {

    display.fillCircle(x + 4, y + 4, 1, SSD1306_WHITE);
    return;
  }

  display.drawPixel(x + 1, y + 1, SSD1306_WHITE);
  display.drawLine(x + 2, y + 0, x + 5, y + 0, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 1, SSD1306_WHITE);
  display.drawLine(x + 2, y + 3, x + 5, y + 3, SSD1306_WHITE);
  display.drawPixel(x + 3, y + 6, SSD1306_WHITE);
  display.drawPixel(x + 4, y + 6, SSD1306_WHITE);
}


void drawFace() {
  display.clearDisplay();
  drawWifiIcon(118, 2);

  EyeEmotion targetEmotion = EMOTION_NEUTRAL;


  if (showHeart && (millis() - heartStartTime < 3500)) {
    targetEmotion = EMOTION_LOVE;
  } else {
    showHeart = false;

    if (!probeConnected) {
      targetEmotion = EMOTION_CURIOUS;
    } else if (mood == "thirsty") {
      targetEmotion = EMOTION_SAD;
    } else if (mood == "dizzy") {
      targetEmotion = EMOTION_SCARED;
    } else {
      targetEmotion = EMOTION_NEUTRAL;
    }
  }


  if (currentEmotion != targetEmotion) {
    currentEmotion = targetEmotion;
    eyes.setEmotionWithTransition(targetEmotion, 400);
  }


  eyes.update();


  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (showHeart) {
    display.setCursor(22, 54);
    display.print("I LOVE YOU! <3");
  } else if (!probeConnected) {
    display.setCursor(6, 52);
    display.print("PLUG SENSOR INTO A0");
  } else if (mood == "thirsty") {
    display.setCursor(18, 54);
    display.print("THIRSTY! WATER ME");
  } else if (mood == "dizzy") {
    display.setCursor(14, 54);
    display.print("TOO WET! DROWNING");
  } else {
    display.setCursor(24, 54);
    display.printf("FEELING GREAT! %d%%", moisture);
  }

  display.display();
}


void drawDiagnostics() {
  display.clearDisplay();
  drawWifiIcon(118, 1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("FLORI");
  display.setCursor(50, 0);
  display.print(probeConnected ? "[SENSOR OK]" : "[NO SENSOR]");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 16);
  if (probeConnected) {
    display.printf("Soil Moist: %d%%", moisture);
    display.setCursor(95, 16);
    display.print(moisture < 35 ? "[DRY]" : (moisture > 75 ? "[WET]" : "[OK]"));
  } else {
    display.print("Soil Moist: NOT CONNECTED");
  }

  display.setCursor(0, 28);
  display.printf("Air Temp  : %.1f C", t);

  display.setCursor(0, 40);
  display.printf("Air Humid : %.0f %%", h);

  display.setCursor(0, 52);
  display.printf("A0:%d | %s", rawMoist, WiFi.status() == WL_CONNECTED ? "WIFI" : "OFFLINE");

  display.display();
}


void syncWithVercel() {
  if (WiFi.status() == WL_CONNECTED && strlen(vercelUrl) > 12) {
    if (!secureClient) return;

    HTTPClient https;
    if (https.begin(*secureClient, vercelUrl)) {
      https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      https.addHeader("Content-Type", "application/json");

      String payload = "{\"t\":" + String(t, 1) +
                       ",\"h\":" + String(h, 1) +
                       ",\"m\":" + String(moisture) +
                       ",\"probeConnected\":" + (probeConnected ? "true" : "false") +
                       ",\"mood\":\"" + mood + "\"" +
                       ",\"status\":\"" + statusMsg + "\"" +
                       ",\"pets\":" + String(petCount) + "}";

      int httpCode = https.POST(payload);
      if (httpCode > 0) {
        Serial.printf("[Vercel] Sync Success: HTTP %d\n", httpCode);
      } else {
        Serial.printf("[Vercel] Sync Failed: %s\n", https.errorToString(httpCode).c_str());
      }
      https.end();
    }
  }
}


void readSensors() {
  rawMoist = analogRead(MOISTURE_PIN);

  if (rawMoist < DISCONNECTED_THRESHOLD) {
    probeConnected = false;
    moisture = -1;
    mood = "searching";
    statusMsg = "Sensor Not Connected ⚠️";
  } else {
    probeConnected = true;
    int calcM = map(rawMoist, DRY_VAL, WET_VAL, 0, 100);
    moisture = constrain(calcM, 0, 100);

    if (moisture < 25) {
      mood = "thirsty";
      statusMsg = "Thirsty! 🪣";
    } else if (moisture > 75) {
      mood = "dizzy";
      statusMsg = "Too Wet! 🌊";
    } else {
      mood = "happy";
      statusMsg = "Optimal 🌿";
    }
  }

  float newH = dht.readHumidity();
  float newT = dht.readTemperature();
  if (!isnan(newH) && !isnan(newT)) {
    h = newH;
    t = newT;
  }


  Serial.printf("[Sensors] Raw A0: %d | Calc Moist: %d%% | Temp: %.1fC | Hum: %.0f%% | Mood: %s\n",
                rawMoist, moisture, t, h, mood.c_str());


  String msg = "{\"t\":" + String(t,1) +
               ",\"h\":" + String(h,1) +
               ",\"m\":" + String(moisture) +
               ",\"connected\":" + (probeConnected ? "true" : "false") +
               ",\"raw\":" + String(rawMoist) +
               ",\"mood\":\"" + mood + "\"" +
               ",\"status\":\"" + statusMsg + "\"" +
               ",\"pets\":" + String(petCount) + "}";
  webSocket.broadcastTXT(msg);
}


void checkButton() {
  bool btnState = digitalRead(BUTTON_PIN);

  if (btnState == LOW && lastButtonState == HIGH) {
    buttonPressStart = millis();
    longPressTriggered = false;
  }

  if (btnState == LOW && !longPressTriggered) {
    if (millis() - buttonPressStart > 1000) {
      currentMode = (currentMode == MODE_FACE) ? MODE_DIAGNOSTIC : MODE_FACE;
      longPressTriggered = true;
    }
  }

  if (btnState == HIGH && lastButtonState == LOW) {
    if (!longPressTriggered && (millis() - buttonPressStart > 40)) {
      heartStartTime = millis();
      showHeart = true;
      petCount++;
      currentMode = MODE_FACE;
      eyes.playWinkLeft();

      String petMsg = "{\"action\":\"pet\",\"pets\":" + String(petCount) + "}";
      webSocket.broadcastTXT(petMsg);


      pendingSync = true;
    }
  }

  lastButtonState = btnState;
}
void setup();
void loop();
#line 399 "C:/Users/1sree/Desktop/newhss-tech/weather/weather-station/weather-station.ino"
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(D2, D1);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  dht.begin();


  secureClient = new BearSSL::WiFiClientSecure();
  secureClient->setInsecure();
  secureClient->setTimeout(3000);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20);
  display.print("FLORI STARTING...");
  display.setCursor(18, 36);
  display.print("Smart Plant Companion");
  display.display();
  delay(1000);


  eyes.begin(&display, SCREEN_WIDTH, SCREEN_HEIGHT, 50);
  eyes.setAutoblinker(true);
  eyes.setBreathing(true);
  eyes.setIdleMovement(true);
  eyes.setEmotion(EMOTION_NEUTRAL);


  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  for (int i = 0; i < numNetworks; i++) {
    Serial.printf("\nTrying Wi-Fi: %s", networks[i].ssid);
    WiFi.begin(networks[i].ssid, networks[i].password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(50);
      display.clearDisplay();
      eyes.update();

      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(12, 54);
      display.printf("CONNECTING: %s", networks[i].ssid);

      display.display();
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected to %s\n", networks[i].ssid);
      break;
    }
    WiFi.disconnect();
  }

  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.begin();
  webSocket.begin();

  readSensors();
}


void loop() {
  unsigned long now = millis();

  server.handleClient();
  webSocket.loop();

  if (WiFi.status() == WL_CONNECTED && !mdnsStarted) {
    if (MDNS.begin("flori")) {
      mdnsStarted = true;
      MDNS.addService("http", "tcp", 80);
      MDNS.addService("ws", "tcp", 81);
    }
  }
  if (mdnsStarted) MDNS.update();

  if (now - lastAnimTime >= 30) {
    lastAnimTime = now;
    checkButton();

    if (currentMode == MODE_FACE) {
      drawFace();
    } else {
      drawDiagnostics();
    }
  }

  if (now - lastSensorTime >= 1000) {
    lastSensorTime = now;
    readSensors();
  }


  if (now - lastVercelSync >= 60000) {
    lastVercelSync = now;
    syncWithVercel();
  }


  if (pendingSync && !showHeart) {
    syncWithVercel();
    pendingSync = false;
    lastVercelSync = now;
  }
}