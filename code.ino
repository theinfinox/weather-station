/*************************************************************************
 * FLORI - Smart Emotive Plant Companion & Environmental Monitor
 * Designed for NodeMCU ESP8266 (Crash-Free, Self-Healing, Online/Offline)
 * 
 * Hardware Connections (PRESERVED - DO NOT CHANGE):
 *   - DHT11 Sensor        : Data -> D4, VCC -> 3.3V, GND -> GND
 *   - 0.96" SSD1306 OLED  : SCL -> D1, SDA -> D2, VCC -> 3.3V, GND -> GND
 *   - Soil Moisture Sensor: Analog AO -> A0, VCC -> 3.3V, GND -> GND
 *   - Push Button         : Leg 1 -> D5, Leg 2 -> GND (Internal INPUT_PULLUP)
 *************************************************************************/

#define BLYNK_TEMPLATE_ID "TMPL3uJXE43xc"
#define BLYNK_TEMPLATE_NAME "weather"
#define BLYNK_AUTH_TOKEN "sH4tpASBtrraaKt3Ouxxq0CJvKhUlLjk"

char ssid[] = "esp";
char pass[] = "esp12345";

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// Pin Definitions
#define DHTPIN        D4
#define DHTTYPE       DHT11
#define BUTTON_PIN    D5
#define MOISTURE_PIN  A0
#define OLED_ADDR     0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// Calibration for Soil Moisture (0-1023 ADC)
// In air/dry soil: ~850, in water/saturated soil: ~350
const int DRY_VAL = 850;
const int WET_VAL = 350;

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
BlynkTimer timer;

// Runtime Metrics
float t = 24.0, h = 60.0;
int rawMoist = 500;
int moisture = 50;
String mood = "happy";
String statusMsg = "Optimal 🌿";
int petCount = 0;
bool mdnsStarted = false;

// Display & Animation State
enum DisplayMode { MODE_FACE, MODE_DIAGNOSTIC };
DisplayMode currentMode = MODE_FACE;
unsigned long heartUntil = 0;
int blinkState = 0; // 0 = open, 1 = closing, 2 = closed, 3 = opening
int blinkStep = 0;
unsigned long lastBlinkTime = 0;
int tearY = 0;

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
  <p id='moodText'>Mood: Evaluating...</p>
  <div class='pills'>
    <div class='pill'>Moisture: <span id='m' class='val moist'>--</span>%</div>
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
  document.getElementById('m').textContent = d.m;
  document.getElementById('t').textContent = d.t.toFixed(1);
  document.getElementById('h').textContent = d.h.toFixed(1);
  document.getElementById('moodText').textContent = 'Status: ' + d.status;
  let time = new Date().toLocaleTimeString();
  chart.data.labels.push(time);
  chart.data.datasets[0].data.push(d.m);
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
                ",\"mood\":\"" + mood + "\"" +
                ",\"status\":\"" + statusMsg + "\"" +
                ",\"pets\":" + String(petCount) + "}";
  server.send(200, "application/json", json);
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

  // If in Loved/Petted Mode (Button pressed)
  if (millis() < heartUntil) {
    drawHeart(40, 30, 24);
    drawHeart(88, 30, 24);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 55);
    display.print("I LOVE YOU! <3");
    display.display();
    return;
  }

  // Base Eye Centers
  int leftEyeX = 40;
  int rightEyeX = 88;
  int eyeY = 28;
  int eyeW = 28;
  int eyeH = 34;

  // Blinking Routine
  if (blinkState > 0) {
    if (blinkState == 1) { // closing
      eyeH = 14;
    } else if (blinkState == 2) { // closed
      eyeH = 4;
    } else if (blinkState == 3) { // opening
      eyeH = 18;
    }
  }

  // Expression based on Soil Moisture
  if (mood == "thirsty") {
    // Drooping sad eyes
    display.fillRoundRect(leftEyeX - eyeW/2, eyeY - eyeH/2 + 4, eyeW, eyeH - 4, 6, SSD1306_WHITE);
    display.fillRoundRect(rightEyeX - eyeW/2, eyeY - eyeH/2 + 4, eyeW, eyeH - 4, 6, SSD1306_WHITE);
    // Cut out top angle to make them look sad
    display.fillTriangle(leftEyeX - eyeW/2, eyeY - eyeH/2, leftEyeX + eyeW/2, eyeY - eyeH/2, leftEyeX + eyeW/2, eyeY - eyeH/2 + 10, SSD1306_BLACK);
    display.fillTriangle(rightEyeX - eyeW/2, eyeY - eyeH/2, rightEyeX + eyeW/2, eyeY - eyeH/2, rightEyeX - eyeW/2, eyeY - eyeH/2 + 10, SSD1306_BLACK);
    // Tear drops
    tearY = (tearY + 3) % 20;
    display.fillCircle(leftEyeX + 6, eyeY + eyeH/2 + tearY, 2, SSD1306_WHITE);
    display.fillCircle(rightEyeX - 6, eyeY + eyeH/2 + tearY, 2, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(18, 54);
    display.print("THIRSTY! WATER ME");

  } else if (mood == "dizzy") {
    // Overwatered: concentric dizzy squint
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
    // Happy / Optimal Content Eyes
    display.fillRoundRect(leftEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);
    display.fillRoundRect(rightEyeX - eyeW/2, eyeY - eyeH/2, eyeW, eyeH, 8, SSD1306_WHITE);

    // Cute eye reflections (pupil sparkle)
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
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("FLORI PLANT MONITOR");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 16);
  display.printf("Soil Moist: %d%%", moisture);
  display.setCursor(95, 16);
  display.print(moisture < 35 ? "[DRY]" : (moisture > 75 ? "[WET]" : "[OK]"));

  display.setCursor(0, 28);
  display.printf("Air Temp  : %.1f C", t);

  display.setCursor(0, 40);
  display.printf("Air Humid : %.0f %%", h);

  display.setCursor(0, 52);
  display.printf("Petted: %d | %s", petCount, WiFi.status() == WL_CONNECTED ? "ONLINE" : "OFFLINE");

  display.display();
}

// ---- Core Logic: Read Sensors & Classify Mood ----
void readSensors() {
  // 1. Read Soil Moisture (Analog A0)
  rawMoist = analogRead(MOISTURE_PIN);
  int calcM = map(rawMoist, DRY_VAL, WET_VAL, 0, 100);
  moisture = constrain(calcM, 0, 100);

  // 2. Read DHT11
  float newH = dht.readHumidity();
  float newT = dht.readTemperature();
  if (!isnan(newH) && !isnan(newT)) {
    h = newH;
    t = newT;
  }

  // 3. Emotional Classification
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

  // 4. Cloud Telemetry Sync (Blynk)
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, t);
    Blynk.virtualWrite(V1, h);
    Blynk.virtualWrite(V2, moisture);
    Blynk.virtualWrite(V3, statusMsg);
  }

  // 5. Local WebSockets Broadcast
  String msg = "{\"t\":" + String(t,1) + 
               ",\"h\":" + String(h,1) + 
               ",\"m\":" + String(moisture) + 
               ",\"mood\":\"" + mood + "\"" + 
               ",\"status\":\"" + statusMsg + "\"" +
               ",\"pets\":" + String(petCount) + "}";
  webSocket.broadcastTXT(msg);
}

// ---- Interactive Button Handling (Debounced) ----
void checkButton() {
  bool btnState = digitalRead(BUTTON_PIN);

  // Button Pressed (Active LOW)
  if (btnState == LOW && lastButtonState == HIGH) {
    buttonPressStart = millis();
    longPressTriggered = false;
  }

  // Button Being Held
  if (btnState == LOW && !longPressTriggered) {
    if (millis() - buttonPressStart > 1000) { // Held for >1 second
      // Toggle Display Mode
      currentMode = (currentMode == MODE_FACE) ? MODE_DIAGNOSTIC : MODE_FACE;
      longPressTriggered = true;
    }
  }

  // Button Released
  if (btnState == HIGH && lastButtonState == LOW) {
    if (!longPressTriggered && (millis() - buttonPressStart > 40)) { // Valid click
      // Trigger "Pet / Loved" reaction!
      heartUntil = millis() + 3500;
      petCount++;
      currentMode = MODE_FACE; // Return to face to enjoy the hearts
      // Broadcast pet event immediately
      webSocket.broadcastTXT("{\"action\":\"pet\",\"pets\":" + String(petCount) + "}");
    }
  }

  lastButtonState = btnState;
}

// ---- Wi-Fi Resilience Routine (Non-Blocking) ----
void checkWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!mdnsStarted) {
      if (MDNS.begin("flori")) {
        mdnsStarted = true;
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
      }
    }
    if (!Blynk.connected()) {
      Blynk.connect(1500); // Non-blocking attempt with 1.5s timeout
    }
  }
}

// ---- Setup ----
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Init Hardware
  Wire.begin(D2, D1); // SDA = D2, SCL = D1 (PRESERVED)
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  dht.begin();

  // Welcome Screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20);
  display.print("FLORI STARTING...");
  display.setCursor(18, 36);
  display.print("Smart Plant Companion");
  display.display();
  delay(1200);

  // Wi-Fi Setup (Self-Healing, Non-blocking)
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to Wi-Fi...");

  // Non-blocking Blynk Config
  Blynk.config(BLYNK_AUTH_TOKEN);

  // Local Web Server & WebSocket
  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.begin();
  webSocket.begin();

  // Timers
  timer.setInterval(1000L, readSensors);      // Sample sensors every 1s
  timer.setInterval(5000L, checkWifiStatus);  // Health check Wi-Fi every 5s

  // Face Animation Timer (30ms = ~33 FPS smooth rendering)
  timer.setInterval(30L, []() {
    checkButton();

    // Blink sequencing
    if (millis() - lastBlinkTime > 3800) {
      blinkState = 1;
      lastBlinkTime = millis();
    } else if (blinkState == 1 && millis() - lastBlinkTime > 80) {
      blinkState = 2;
    } else if (blinkState == 2 && millis() - lastBlinkTime > 140) {
      blinkState = 3;
    } else if (blinkState == 3 && millis() - lastBlinkTime > 200) {
      blinkState = 0;
    }

    if (currentMode == MODE_FACE) {
      drawFace();
    } else {
      drawDiagnostics();
    }
  });
}

// ---- Main Loop ----
void loop() {
  if (Blynk.connected()) Blynk.run();
  timer.run();
  server.handleClient();
  webSocket.loop();
  if (mdnsStarted) MDNS.update();
}