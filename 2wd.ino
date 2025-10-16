/********************************************************
 * Production-grade 2WD Robot (NodeMCU + 1x L298N)
 * - Blynk IoT (v2) using Blynk.config() + Blynk.connect()
 * - Joystick X->V1, Y->V2
 * - Sliders: V3 (Max Speed 0..1023), V4 (Curve alpha 0..1570),
 *            V5 (Deadzone 0..200)
 * - Local webserver + WebSocket for telemetry (port 80/81)
 * - mDNS optional for local name resolution "robot.local"
 ********************************************************/

/* ------------------ Blynk / WiFi ------------------ */
#define BLYNK_TEMPLATE_ID "TMPL3xyV5ML0r"
#define BLYNK_TEMPLATE_NAME "JoyStick"
#define BLYNK_AUTH_TOKEN "y78pGzUp3XacDd_vraO4PlSoyt7-PSEi"

char ssid[] = "esp";
char pass[] = "esp12345";

/* ------------------ Libraries ------------------ */
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsServer.h>

/* ------------------ Pin mapping (EDIT TO MATCH YOUR BOARD) ------------------ */
// Left motors (Front + Rear)
#define LEFT_IN1 D5
#define LEFT_IN2 D6
#define LEFT_EN  D1   // PWM

// Right motors (Front + Rear)
#define RIGHT_IN1 D7
#define RIGHT_IN2 D8
#define RIGHT_EN  D2  // PWM

/* ------------------ Telemetry / Server ------------------ */
ESP8266WebServer server(80);
WebSocketsServer webSocket(81);
bool mdnsStarted = false;

/* ------------------ Control parameters ------------------ */
BlynkTimer timer;

// runtime variables
volatile int vJoyX = 0;       
volatile int vJoyY = 0;
volatile int vMax = 800;      
volatile int vAlphaSlider = 1000; 
volatile int vDead = 40;

float maxSpeed = 800.0;
float alpha = 1.0;
int deadzone = 40;

// telemetry values
float telemetry_left_pwm = 0.0;
float telemetry_right_pwm = 0.0;

/* ------------------ Helpers ------------------ */
void safePinModeOut(int pin){
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void motorDrive(int in1, int in2, int enPin, float pwm) {
  pwm = constrain(pwm, -1.0, 1.0);
  if (pwm > 0.01) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
    analogWrite(enPin, int(pwm * 1023.0));
  } else if (pwm < -0.01) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
    analogWrite(enPin, int(-pwm * 1023.0));
  } else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
    analogWrite(enPin, 0);
  }
}

float applyTanCurve(float v, float alphaRad) {
  if (fabs(v) < 1e-6) return 0.0;
  float s = (v > 0) ? 1.0 : -1.0;
  float a = fabs(v);
  float denom = tan(alphaRad);
  if (denom == 0.0) return v;
  return s * (tan(alphaRad * a) / denom);
}

/* ------------------ Web handlers ------------------ */
const char STATUS_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Robot Telemetry</title></head><body style='font-family:sans-serif'>
<h3>Robot Telemetry</h3>
<p>Left PWM: <span id='l'>--</span></p>
<p>Right PWM: <span id='r'>--</span></p>
<script>
const ws=new WebSocket('ws://'+location.hostname+':81/');
ws.onmessage=e=>{
  const d=JSON.parse(e.data);
  document.getElementById('l').textContent=d.left.toFixed(3);
  document.getElementById('r').textContent=d.right.toFixed(3);
};
</script></body></html>
)rawliteral";

void handleRoot(){
  server.send_P(200, "text/html", STATUS_PAGE);
}

void handleJson(){
  String j = "{\"left\":" + String(telemetry_left_pwm,3) + ",\"right\":" + String(telemetry_right_pwm,3) + "}";
  server.send(200, "application/json", j);
}

/* ------------------ Core control loop ------------------ */
void updateMotorsAndTelemetry(){
  float fx = (float)vJoyX / 1023.0;
  float fy = (float)vJoyY / 1023.0;

  float r = sqrt(fx*fx + fy*fy);
  if (r * 1023.0 < deadzone) { fx = 0; fy = 0; }

  fx = constrain(fx, -1.0, 1.0);
  fy = constrain(fy, -1.0, 1.0);

  float left = fy + fx;
  float right = fy - fx;

  float maxAbs = max(fabs(left), fabs(right));
  if (maxAbs > 1.0) { left /= maxAbs; right /= maxAbs; }

  float leftC = applyTanCurve(left, alpha);
  float rightC = applyTanCurve(right, alpha);

  float scale = maxSpeed / 1023.0;
  leftC *= scale;
  rightC *= scale;

  motorDrive(LEFT_IN1, LEFT_IN2, LEFT_EN, leftC);
  motorDrive(RIGHT_IN1, RIGHT_IN2, RIGHT_EN, rightC);

  telemetry_left_pwm = leftC;
  telemetry_right_pwm = rightC;

  Blynk.virtualWrite(V10, telemetry_left_pwm);
  Blynk.virtualWrite(V11, telemetry_right_pwm);
}

/* ------------------ Blynk handlers ------------------ */
BLYNK_WRITE(V1){ vJoyX = param.asInt(); }
BLYNK_WRITE(V2){ vJoyY = param.asInt(); }
BLYNK_WRITE(V3){ vMax = param.asInt(); maxSpeed = (float)vMax; }
BLYNK_WRITE(V4){
  vAlphaSlider = param.asInt();
  alpha = ((float)vAlphaSlider / 1570.0) * (PI/2.0);
  alpha = constrain(alpha, 0.01, (PI/2.0) - 0.01);
}
BLYNK_WRITE(V5){ vDead = param.asInt(); deadzone = vDead; }

/* ------------------ WebSocket event ------------------ */
void wsEvent(uint8_t /*num*/, WStype_t /*type*/, uint8_t* /*payload*/, size_t /*length*/) {}

/* ------------------ Setup & Loop ------------------ */
void setup(){
  Serial.begin(115200);
  delay(50);

  safePinModeOut(LEFT_IN1);
  safePinModeOut(LEFT_IN2);
  pinMode(LEFT_EN, OUTPUT);

  safePinModeOut(RIGHT_IN1);
  safePinModeOut(RIGHT_IN2);
  pinMode(RIGHT_EN, OUTPUT);

  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-start < 15000UL){
    delay(250); Serial.print(".");
  }
  Serial.println("\nWiFi connected: " + WiFi.localIP().toString());

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(5000);

  server.on("/", handleRoot);
  server.on("/json", handleJson);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(wsEvent);

  if (MDNS.begin("robot")) {
    mdnsStarted = true;
    MDNS.addService("http","tcp",80);
    MDNS.addService("ws","tcp",81);
    Serial.println("mDNS running: http://robot.local");
  }

  timer.setInterval(33L, updateMotorsAndTelemetry);

  timer.setInterval(200L, [](){
    String msg = "{\"left\":" + String(telemetry_left_pwm,3) + ",\"right\":" + String(telemetry_right_pwm,3) + "}";
    webSocket.broadcastTXT(msg);
  });

  timer.setInterval(5000L, [](){
    if (!Blynk.connected()) Blynk.connect(3000);
  });
}

void loop(){
  Blynk.run();
  timer.run();
  server.handleClient();
  webSocket.loop();
  if (mdnsStarted) MDNS.update();
}