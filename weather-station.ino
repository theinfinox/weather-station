/*************************************************************************
 * FLORI - Smart Emotive Plant Companion & Environmental Monitor
 * Designed for NodeMCU ESP8266 (100% Vercel Cloud + Local WebSockets)
 * Single Master Source: Compiles in both PlatformIO & Arduino IDE
 * 
 * Hardware Connections (PRESERVED - DO NOT CHANGE):
 *   - DHT11 Sensor        : Data -> D4, VCC -> 3.3V, GND -> GND
 *   - 0.96" SSD1306 OLED  : SCL -> D1, SDA -> D2, VCC -> 3.3V, GND -> GND
 *   - Soil Moisture Sensor: Analog AO -> A0, VCC -> 3.3V, GND -> GND
 *   - Push Button         : Leg 1 -> D5, Leg 2 -> GND (Internal INPUT_PULLUP)
 *************************************************************************/

#include <Arduino.h>

// ---- Wi-Fi Configuration ----
struct WiFiCredential {
    const char* ssid;
    const char* password;
};

WiFiCredential networks[] = {
    {"esp", "esp12345"},
    {"pi", "raspberry"}
};
const int numNetworks = sizeof(networks) / sizeof(networks[0]);

// ---- Vercel Cloud API Endpoint ----
// Replace with your actual Vercel project domain (e.g. "https://my-plant.vercel.app/api/update")
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

// Forward Function Prototypes (Enables strict C++ compatibility for PlatformIO)
void handleRoot();
void handleJson();
void drawWifiIcon(int x, int y);
void drawFace();
void drawDiagnostics();
bool syncWithVercel();
void readSensors();
void checkButton();

// Pin Definitions
#define DHTPIN        D4
#define DHTTYPE       DHT11
#define BUTTON_PIN    D5
#define MOISTURE_PIN  A0
#define OLED_ADDR     0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Calibration for Soil Moisture (0-1023 ADC)
// When probe is unplugged, NodeMCU internal divider pulls A0 down near 0 (<35 counts)
const int DISCONNECTED_THRESHOLD = 35;
const int DRY_VAL = 1024;
const int WET_VAL = 350;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
RobotEyes eyes;

// Runtime Metrics
float t = 24.0, h = 60.0;
int rawMoist = 0;
int moisture = -1;
bool probeConnected = false;
String mood = "searching";
String statusMsg = "Sensor Not Connected ⚠️";
bool mdnsStarted = false;

// Display & Animation State
enum DisplayMode { MODE_FACE, MODE_DIAGNOSTIC };
DisplayMode currentMode = MODE_FACE;
EyeEmotion currentEmotion = EMOTION_NEUTRAL;

// Non-blocking Timer Tracking (Zero external timer dependencies)
unsigned long lastAnimTime = 0;
unsigned long lastSensorTime = 0;
unsigned long lastVercelSync = -60000; // Trigger an immediate sync on boot!

// Button Debounce State
bool lastButtonState = HIGH;
unsigned long buttonPressStart = 0;

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

// ---- Web Handlers ----
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
                ",\"status\":\"" + statusMsg + "\"}";
  server.send(200, "application/json", json);
}

// ---- Helpers: Draw Mini Wi-Fi Icon / Offline Dot in Top Corner ----
void drawWifiIcon(int x, int y) {
  bool online = (WiFi.status() == WL_CONNECTED);
  if (!online) {
    // Draw a small dot when offline
    display.fillCircle(x + 4, y + 4, 1, SSD1306_WHITE);
    return;
  }
  // Small WiFi icon (8x7 px)
  display.drawPixel(x + 1, y + 1, SSD1306_WHITE);
  display.drawLine(x + 2, y + 0, x + 5, y + 0, SSD1306_WHITE);
  display.drawPixel(x + 6, y + 1, SSD1306_WHITE);
  display.drawLine(x + 2, y + 3, x + 5, y + 3, SSD1306_WHITE);
  display.drawPixel(x + 3, y + 6, SSD1306_WHITE);
  display.drawPixel(x + 4, y + 6, SSD1306_WHITE);
}

// ---- Graphics Engine: Render Animated Robo Face ----
void drawFace() {
  display.clearDisplay();
  drawWifiIcon(118, 2);

  EyeEmotion targetEmotion = EMOTION_NEUTRAL;

  // Determine the appropriate emotion based on plant physiological state
  if (!probeConnected) {
    targetEmotion = EMOTION_CURIOUS; // Searching for probe
  } else if (mood == "thirsty") {
    targetEmotion = EMOTION_SAD; // Needs hydration
  } else if (mood == "dizzy") {
    targetEmotion = EMOTION_SCARED; // Overwatered
  } else {
    targetEmotion = EMOTION_NEUTRAL; // Optimal conditions
  }

  // Apply emotion smoothly when it changes
  if (currentEmotion != targetEmotion) {
    currentEmotion = targetEmotion;
    eyes.setEmotionWithTransition(targetEmotion, 400); // 400ms smooth morph
  }

  // Render the animated eyes
  eyes.update();

  // Render context-aware text
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  if (!probeConnected) {
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

// ---- Graphics Engine: Diagnostic Telemetry Screen ----
void drawDiagnostics() {
  display.clearDisplay();
  drawWifiIcon(118, 1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("FLORI DIAGNOSTICS");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);

  display.setCursor(0, 13);
  if (probeConnected) {
    display.printf("Soil Moist: %d%% %s", moisture, (moisture < 25 ? "[DRY]" : (moisture > 75 ? "[WET]" : "[OK]")));
  } else {
    display.print("Soil Moist: [DISCONNECTED]");
  }

  display.setCursor(0, 25);
  display.printf("Air Temp  : %.1f C", t);

  display.setCursor(0, 37);
  display.printf("Air Humid : %.0f %%", h);

  display.setCursor(0, 49);
  if (WiFi.status() == WL_CONNECTED) {
    display.printf("IP: %s", WiFi.localIP().toString().c_str());
  } else {
    display.printf("A0: %d [OFFLINE]", rawMoist);
  }

  display.display();
}

// ---- Industrial-Grade Vercel Cloud Sync Routine ----
bool syncWithVercel() {
  if (WiFi.status() != WL_CONNECTED || strlen(vercelUrl) < 12) {
    Serial.println(F("[Vercel] Skipped: WiFi offline"));
    return false;
  }

  // Ensure an IP address is allocated before hitting network
  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println(F("[Vercel] Skipped: No IP address assigned"));
    return false;
  }

  String payload = "{\"t\":" + String(t, 1) + 
                   ",\"h\":" + String(h, 1) + 
                   ",\"m\":" + String(moisture) + 
                   ",\"probeConnected\":" + (probeConnected ? "true" : "false") +
                   ",\"mood\":\"" + mood + "\"" +
                   ",\"status\":\"" + statusMsg + "\"}";

  // Two-stage retry loop (1 primary attempt + 1 automatic failover retry)
  for (int attempt = 1; attempt <= 2; attempt++) {
    BearSSL::WiFiClientSecure client;
    client.setInsecure(); // Bypass CA verification for serverless endpoints
    client.setTimeout(8000); // 8-second SSL timeout
    
    // CRITICAL: Restrict buffer sizes to 1024 RX / 512 TX.
    // Saves over 30KB of contiguous heap, preventing malloc() NULL crashes!
    client.setBufferSizes(1024, 512);

    HTTPClient https;
    if (https.begin(client, vercelUrl)) {
      https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      https.addHeader("Content-Type", "application/json");
      https.addHeader("User-Agent", "ESP8266-Flori/1.0");
      https.addHeader("Connection", "close"); // Avoid socket hangs on edge proxy

      int httpCode = https.POST(payload);
      
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
        Serial.printf("[Vercel] Sync Success: HTTP %d (Free Heap: %d bytes)\n", httpCode, ESP.getFreeHeap());
        https.end();
        client.stop();
        return true;
      } else if (httpCode > 0) {
        Serial.printf("[Vercel] Server Response: HTTP %d (attempt %d/2)\n", httpCode, attempt);
      } else {
        Serial.printf("[Vercel] Transport Error: %s (attempt %d/2)\n", https.errorToString(httpCode).c_str(), attempt);
      }
      https.end();
    } else {
      Serial.printf("[Vercel] Failed to initialize HTTP client (attempt %d/2)\n", attempt);
    }
    client.stop();

    if (attempt < 2) {
      delay(1500); // Wait 1.5s before retry
    }
  }

  return false;
}

// ---- Core Logic: Read Sensors & Classify Mood ----
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

  // Serial logging for practical calibration
  Serial.printf("[Sensors] Raw A0: %d | Calc Moist: %d%% | Temp: %.1fC | Hum: %.0f%% | Mood: %s\n", 
                rawMoist, moisture, t, h, mood.c_str());

  // Broadcast to local WebSocket clients
  String msg = "{\"t\":" + String(t,1) + 
               ",\"h\":" + String(h,1) + 
               ",\"m\":" + String(moisture) + 
               ",\"connected\":" + (probeConnected ? "true" : "false") +
               ",\"raw\":" + String(rawMoist) +
               ",\"mood\":\"" + mood + "\"" + 
               ",\"status\":\"" + statusMsg + "\"}";
  webSocket.broadcastTXT(msg);
}

// ---- Interactive Button Handling (Debounced Mode Toggle) ----
void checkButton() {
  bool btnState = digitalRead(BUTTON_PIN);

  if (btnState == LOW && lastButtonState == HIGH) {
    buttonPressStart = millis();
  }

  // Toggle between Emotive Face & Engineering Diagnostics on button press
  if (btnState == HIGH && lastButtonState == LOW) {
    if (millis() - buttonPressStart > 50) {
      currentMode = (currentMode == MODE_FACE) ? MODE_DIAGNOSTIC : MODE_FACE;
      Serial.printf("[Mode] Toggled to %s\n", (currentMode == MODE_FACE ? "Face View" : "Diagnostic View"));
    }
  }

  lastButtonState = btnState;
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(D2, D1);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  dht.begin();

  // Disable WiFi sleep mode to guarantee maximum packet transmission reliability
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20);
  display.print("FLORI STARTING...");
  display.setCursor(18, 36);
  display.print("Smart Plant Companion");
  display.display();
  delay(1000);

  // Initialize RobotEyes
  eyes.begin(&display, SCREEN_WIDTH, SCREEN_HEIGHT, 50);
  eyes.setAutoblinker(true);
  eyes.setBreathing(true);
  eyes.setIdleMovement(true);
  eyes.setEmotion(EMOTION_NEUTRAL);

  // Multi-Network Wi-Fi Connection
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
      eyes.update(); // Keep face animated while waiting!
      
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
  syncWithVercel(); // Trigger immediate sync on boot
  lastVercelSync = millis();
}

// ---- Main Non-Blocking Loop ----
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

  if (now - lastSensorTime >= 2500) {
    lastSensorTime = now;
    readSensors();
  }

  // Sync with Vercel every 20 seconds (instead of 60s)
  if (now - lastVercelSync >= 20000) {
    lastVercelSync = now;
    syncWithVercel();
  }
}