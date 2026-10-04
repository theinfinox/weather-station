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
char ssid[] = "esp";
char pass[] = "esp12345";

// ---- Vercel Cloud API Endpoint ----
// Replace with your actual Vercel project domain (e.g. "https://my-plant.vercel.app/api/update")
const char* vercelUrl = "https://weather-station-nu-one.vercel.app/api/update";

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

// Forward Function Prototypes (Enables strict C++ compatibility for PlatformIO)
void handleRoot();
void handleJson();
void drawWifiIcon(int x, int y);
void drawHeart(int x, int y, int size);
void drawFace();
void drawDiagnostics();
void syncWithVercel();
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
const int DRY_VAL = 850;
const int WET_VAL = 350;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

// Runtime Metrics
float t = 24.0, h = 60.0;
int rawMoist = 0;
int moisture = -1;
bool probeConnected = false;
String mood = "searching";
String statusMsg = "Sensor Not Connected ⚠️";
int petCount = 0;
bool mdnsStarted = false;

// Display & Animation State
enum DisplayMode { MODE_FACE, MODE_DIAGNOSTIC };
DisplayMode currentMode = MODE_FACE;
unsigned long heartUntil = 0;
int blinkState = 0;
unsigned long lastBlinkTime = 0;
int tearY = 0;
int searchEyeOffset = 0;
int searchEyeDir = 1;

// Non-blocking Timer Tracking (Zero external timer dependencies)
unsigned long lastAnimTime = 0;
unsigned long lastSensorTime = 0;
unsigned long lastVercelSync = 0;
unsigned long lastSearchEyeTime = 0;

// Button Debounce State
bool lastButtonState = HIGH;
unsigned long buttonPressStart = 0;
bool longPressTriggered = false;

// ---- Local Embedded Web Page (PROGMEM) ----
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Flori - Local Plant Companion</title>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<style>
  body{font-family:sans-serif;background:#070d1e;color:#e2e8f0;text-align:center;padding:16px;margin:0}
  .card{max-width:540px;margin:0 auto;background:#0d1730;padding:20px;border-radius:16px;border:1px solid rgba(255,255,255,0.1)}
  .pills{display:flex;justify-content:center;gap:12px;margin:16px 0;flex-wrap:wrap}
  .pill{padding:8px 14px;border-radius:999px;background:rgba(255,255,255,0.06);font-size:14px}
  .val{font-size:18px;font-weight:bold}
  .moist{color:#10b981}.temp{color:#fb7185}.hum{color:#38bdf8}
  canvas{width:100%!important;max-height:260px}
</style>
</head><body>
<div class='card'>
  <h2>🌱 Flori Local Monitor</h2>
  <p id='moodText'>Evaluating...</p>
  <div class='pills'>
    <div class='pill'>Moisture: <span id='m' class='val moist'>--</span></div>
    <div class='pill'>Temp: <span id='t' class='val temp'>--</span>°C</div>
    <div class='pill'>Hum: <span id='h' class='val hum'>--</span>%</div>
  </div>
  <canvas id='chart'></canvas>
</div>
<script>
const ws = new WebSocket('ws://' + location.hostname + ':81/');
let chart = new Chart(document.getElementById('chart').getContext('2d'), {
  type:'line',
  data:{labels:[],datasets:[
    {label:'Soil Moist %',borderColor:'#10b981',data:[],yAxisID:'y1'},
    {label:'Temp °C',borderColor:'#fb7185',data:[],yAxisID:'y2'},
    {label:'Hum %',borderColor:'#38bdf8',data:[],yAxisID:'y1'}
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
                ",\"status\":\"" + statusMsg + "\"" +
                ",\"pets\":" + String(petCount) + "}";
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

// ---- Helpers: Draw Heart Shape for Petting Reaction ----
void drawHeart(int x, int y, int size) {
  display.fillCircle(x - size/3, y - size/4, size/3, SSD1306_WHITE);
  display.fillCircle(x + size/3, y - size/4, size/3, SSD1306_WHITE);
  display.fillTriangle(x - (size*2)/3, y - size/8, x + (size*2)/3, y - size/8, x, y + (size*2)/3, SSD1306_WHITE);
}

// ---- Graphics Engine: Render Animated Robo Face ----
void drawFace() {
  display.clearDisplay();
  drawWifiIcon(118, 2);

  // 1. If in Loved/Petted Mode (Button pressed)
  if (millis() < heartUntil) {
    drawHeart(40, 28, 24);
    drawHeart(88, 28, 24);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(22, 54);
    display.print("I LOVE YOU! <3");
    display.display();
    return;
  }

  int leftEyeX = 40;
  int rightEyeX = 88;
  int eyeY = 26;
  int eyeW = 28;
  int eyeH = 34;

  // Blinking Routine
  if (blinkState > 0) {
    if (blinkState == 1) eyeH = 14;
    else if (blinkState == 2) eyeH = 4;
    else if (blinkState == 3) eyeH = 18;
  }

  // 2. Expression based on Connection & Soil Moisture
  if (!probeConnected) {
    leftEyeX += searchEyeOffset;
    rightEyeX += searchEyeOffset;

    display.fillRoundRect(leftEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);
    display.fillRoundRect(rightEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);

    if (eyeH > 10) {
      display.fillCircle(leftEyeX + (searchEyeDir * 4), eyeY, 4, SSD1306_BLACK);
      display.fillCircle(rightEyeX + (searchEyeDir * 4), eyeY, 4, SSD1306_BLACK);
    }

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(6, 52);
    display.print("PLUG SENSOR INTO A0");

  } else if (mood == "thirsty") {
    display.fillRoundRect(leftEyeX - eyeW/2, eyeY - eyeH/2 + 4, eyeW, eyeH - 4, 6, SSD1306_WHITE);
    display.fillRoundRect(rightEyeX - eyeW/2, eyeY - eyeH/2 + 4, eyeW, eyeH - 4, 6, SSD1306_WHITE);
    display.fillTriangle(leftEyeX - eyeW/2, eyeY - eyeH/2, leftEyeX + eyeW/2, eyeY - eyeH/2, leftEyeX + eyeW/2, eyeY - eyeH/2 + 10, SSD1306_BLACK);
    display.fillTriangle(rightEyeX - eyeW/2, eyeY - eyeH/2, rightEyeX + eyeW/2, eyeY - eyeH/2, rightEyeX - eyeW/2, eyeY - eyeH/2 + 10, SSD1306_BLACK);

    tearY = (tearY + 3) % 20;
    display.fillCircle(leftEyeX + 6, eyeY + eyeH/2 + tearY, 2, SSD1306_WHITE);
    display.fillCircle(rightEyeX - 6, eyeY + eyeH/2 + tearY, 2, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(18, 54);
    display.print("THIRSTY! WATER ME");

  } else if (mood == "dizzy") {
    display.drawCircle(leftEyeX, eyeY, 14, SSD1306_WHITE);
    display.drawCircle(leftEyeX, eyeY, 8, SSD1306_WHITE);
    display.fillCircle(leftEyeX, eyeY, 3, SSD1306_WHITE);

    display.drawCircle(rightEyeX, eyeY, 14, SSD1306_WHITE);
    display.drawCircle(rightEyeX, eyeY, 8, SSD1306_WHITE);
    display.fillCircle(rightEyeX, eyeY, 3, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(14, 54);
    display.print("TOO WET! DROWNING");

  } else {
    display.fillRoundRect(leftEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);
    display.fillRoundRect(rightEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);

    if (eyeH > 10) {
      display.fillCircle(leftEyeX - 4, eyeY - 6, 3, SSD1306_BLACK);
      display.fillCircle(rightEyeX - 4, eyeY - 6, 3, SSD1306_BLACK);
    }

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
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
  display.print("FLORI MONITOR");
  display.setCursor(80, 0);
  display.print(probeConnected ? "[PROBE OK]" : "[NO PROBE]");
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
  display.printf("Raw A0:%d | %s", rawMoist, WiFi.status() == WL_CONNECTED ? "WIFI ON" : "OFFLINE");

  display.display();
}

// ---- Vercel Cloud Sync Routine (Direct HTTPS POST) ----
void syncWithVercel() {
  if (WiFi.status() == WL_CONNECTED && strlen(vercelUrl) > 12) {
    std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
    client->setInsecure(); // Bypass SSL certificate expiration issues
    client->setTimeout(3000);

    HTTPClient https;
    if (https.begin(*client, vercelUrl)) {
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

    if (moisture < 35) {
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

  // Broadcast to local WebSocket clients
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

// ---- Interactive Button Handling (Debounced) ----
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
      heartUntil = millis() + 3500;
      petCount++;
      currentMode = MODE_FACE;
      String petMsg = "{\"action\":\"pet\",\"pets\":" + String(petCount) + "}";
      webSocket.broadcastTXT(petMsg);
      syncWithVercel();
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

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20);
  display.print("FLORI STARTING...");
  display.setCursor(18, 36);
  display.print("Smart Plant Companion");
  display.display();
  delay(1000);

  // Self-Healing Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);

  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.begin();
  webSocket.begin();

  readSensors();
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

    if (now - lastBlinkTime > 3800) {
      blinkState = 1;
      lastBlinkTime = now;
    } else if (blinkState == 1 && now - lastBlinkTime > 80) {
      blinkState = 2;
    } else if (blinkState == 2 && now - lastBlinkTime > 140) {
      blinkState = 3;
    } else if (blinkState == 3 && now - lastBlinkTime > 200) {
      blinkState = 0;
    }

    if (currentMode == MODE_FACE) {
      drawFace();
    } else {
      drawDiagnostics();
    }
  }

  if (!probeConnected && (now - lastSearchEyeTime >= 120)) {
    lastSearchEyeTime = now;
    searchEyeOffset += searchEyeDir;
    if (searchEyeOffset > 4 || searchEyeOffset < -4) {
      searchEyeDir = -searchEyeDir;
    }
  }

  if (now - lastSensorTime >= 1000) {
    lastSensorTime = now;
    readSensors();
  }

  if (now - lastVercelSync >= 3000) {
    lastVercelSync = now;
    syncWithVercel();
  }
}