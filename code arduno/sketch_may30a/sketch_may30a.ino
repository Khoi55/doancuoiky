#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

// ================= WIFI =================
const char* WIFI_SSID = "Khoideptrai";
const char* WIFI_PASS = "12345678";

// ================= MQTT =================
const char* MQTT_HOST = "broker.emqx.io";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "khoi55/aimangcambien/N22DCDT031/command";

// ================= LED RGB =================
#define LED_R 2
#define LED_G 4
#define LED_B 16

// ================= MOSFET FAN =================
#define FAN_MOSFET_PIN 17  // GPIO 17 -> chân SIG module MOSFET D4184

// ================= SERVER =================
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= STATE =================
String lastLabel = "none";
String lastAction = "none";
float lastConfidence = 0.0;
unsigned long lastReceivedMs = 0;

bool lightOn = false;
bool fanOn = false;  // Trạng thái quạt

void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
}

void setFan(bool on) {
  fanOn = on;
  digitalWrite(FAN_MOSFET_PIN, on ? HIGH : LOW);
  Serial.print("[FAN] MOSFET -> ");
  Serial.println(on ? "ON" : "OFF");
}

void applyCommand(String label, float confidence) {
  lastLabel = label;
  lastConfidence = confidence;
  lastReceivedMs = millis();

  Serial.println("========== MQTT COMMAND ==========");
  Serial.print("Label: ");
  Serial.println(label);
  Serial.print("Confidence: ");
  Serial.println(confidence, 3);

  if (label == "bat_den") {
    lightOn = true;
    setRGB(false, true, false); // xanh lá = đèn bật
    lastAction = "LIGHT_ON";
  }
  else if (label == "tat_den") {
    lightOn = false;
    setRGB(true, false, false); // đỏ = đèn tắt
    lastAction = "LIGHT_OFF";
  }
  else if (label == "bat_quat") {
    setFan(true);                // BẬT quạt qua MOSFET
    setRGB(false, false, true);  // xanh dương = quạt bật
    lastAction = "FAN_ON";
  }
  else if (label == "tat_quat") {
    setFan(false);               // TẮT quạt qua MOSFET
    setRGB(true, false, true);   // tím = quạt tắt
    lastAction = "FAN_OFF";
  }
  else if (label == "dung") {
    // Dừng tất cả
    lightOn = false;
    setFan(false);
    setRGB(true, true, false);   // vàng = dừng
    lastAction = "STOP_ALL";
  }
  else if (label == "unknown") {
    lastAction = "IGNORED_UNKNOWN";
  }
  else if (label == "noise") {
    lastAction = "IGNORED_NOISE";
  }
  else {
    lastAction = "UNKNOWN_LABEL";
  }

  Serial.print("Action: ");
  Serial.println(lastAction);
  Serial.print("Light: ");
  Serial.println(lightOn ? "ON" : "OFF");
  Serial.print("Fan: ");
  Serial.println(fanOn ? "ON" : "OFF");
}

String extractJsonString(String payload, String key) {
  String pattern = "\"" + key + "\":\"";
  int start = payload.indexOf(pattern);
  if (start < 0) return "";

  start += pattern.length();
  int end = payload.indexOf("\"", start);
  if (end < 0) return "";

  return payload.substring(start, end);
}

float extractJsonFloat(String payload, String key) {
  String pattern = "\"" + key + "\":";
  int start = payload.indexOf(pattern);
  if (start < 0) return 0.0;

  start += pattern.length();
  int end = payload.indexOf(",", start);
  if (end < 0) end = payload.indexOf("}", start);
  if (end < 0) return 0.0;

  return payload.substring(start, end).toFloat();
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  Serial.print("[MQTT] Message: ");
  Serial.println(msg);

  String label = extractJsonString(msg, "label");
  float confidence = extractJsonFloat(msg, "confidence");

  if (label.length() == 0) {
    Serial.println("[MQTT] Invalid payload, missing label");
    return;
  }

  if (confidence < 0.70) {
    lastLabel = label;
    lastConfidence = confidence;
    lastAction = "LOW_CONFIDENCE_IGNORED";
    Serial.println("[MQTT] Low confidence ignored");
    return;
  }

  applyCommand(label, confidence);
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] Connecting... ");

    String clientId = "ESP32_KWS_" + String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe(MQTT_TOPIC);

      Serial.print("[MQTT] Subscribed topic: ");
      Serial.println(MQTT_TOPIC);
    }
    else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retry in 2 seconds");
      delay(2000);
    }
  }
}

String htmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
<meta charset="UTF-8">
<title>ESP32 KWS Receiver</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body {
  font-family: 'Segoe UI', Arial, sans-serif;
  background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%);
  color: #e2e8f0;
  margin: 0;
  padding: 20px;
  min-height: 100vh;
}
.container {
  max-width: 760px;
  margin: auto;
}
h1 {
  text-align: center;
  font-size: 22px;
  background: linear-gradient(90deg, #38bdf8, #818cf8);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
.subtitle {
  text-align: center;
  color: #94a3b8;
  font-size: 13px;
  margin-bottom: 20px;
}
.card {
  background: rgba(30, 41, 59, 0.8);
  backdrop-filter: blur(10px);
  padding: 20px;
  border-radius: 16px;
  margin-bottom: 16px;
  border: 1px solid rgba(148, 163, 184, 0.1);
  box-shadow: 0 8px 32px rgba(0,0,0,0.3);
}
.card h2 {
  margin-top: 0;
  font-size: 16px;
  color: #94a3b8;
  text-transform: uppercase;
  letter-spacing: 1px;
}
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.metric {
  background: rgba(15, 23, 42, 0.6);
  border-radius: 12px;
  padding: 16px;
  border: 1px solid rgba(148, 163, 184, 0.08);
  transition: transform 0.2s, border-color 0.2s;
}
.metric:hover {
  transform: translateY(-2px);
  border-color: rgba(56, 189, 248, 0.3);
}
.label {
  color: #64748b;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}
.value {
  font-size: 26px;
  font-weight: 700;
  margin-top: 4px;
  color: #f1f5f9;
}
.on { color: #4ade80 !important; }
.off { color: #f87171 !important; }
.status-dot {
  display: inline-block;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  margin-right: 6px;
  animation: pulse 2s infinite;
}
.dot-on { background: #4ade80; box-shadow: 0 0 8px #4ade80; }
.dot-off { background: #f87171; box-shadow: 0 0 8px #f87171; }
@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}
.device-row {
  display: flex;
  justify-content: space-around;
  margin-top: 10px;
}
.device {
  text-align: center;
  padding: 14px 24px;
  border-radius: 12px;
  background: rgba(15, 23, 42, 0.6);
  border: 1px solid rgba(148, 163, 184, 0.08);
  min-width: 120px;
}
.device-icon { font-size: 32px; }
.device-label { font-size: 12px; color: #94a3b8; margin-top: 4px; }
.device-status { font-size: 18px; font-weight: 700; margin-top: 4px; }
</style>
</head>
<body>
<div class="container">
  <h1>&#127908; ESP32 Vietnamese KWS Dashboard</h1>
  <p class="subtitle">Phone Edge Impulse WASM &#8594; MQTT &#8594; ESP32 &#8594; LED + MOSFET Fan</p>

  <div class="card">
    <h2>&#128300; Device Status</h2>
    <div class="device-row">
      <div class="device">
        <div class="device-icon">&#128161;</div>
        <div class="device-label">Light (LED)</div>
        <div id="light-status" class="device-status off">OFF</div>
      </div>
      <div class="device">
        <div class="device-icon">&#127744;</div>
        <div class="device-label">Fan (MOSFET)</div>
        <div id="fan-status" class="device-status off">OFF</div>
      </div>
    </div>
  </div>

  <div class="card">
    <h2>&#128202; Last Prediction</h2>
    <div class="grid">
      <div class="metric">
        <div class="label">Label</div>
        <div id="label" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Confidence</div>
        <div id="confidence" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Action</div>
        <div id="action" class="value" style="font-size:16px;">--</div>
      </div>
      <div class="metric">
        <div class="label">Uptime</div>
        <div id="uptime" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Free Heap</div>
        <div id="heap" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">MQTT</div>
        <div id="mqtt" class="value">--</div>
      </div>
    </div>
  </div>
</div>

<script>
async function update() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();

    document.getElementById('label').textContent = d.label;
    document.getElementById('confidence').textContent = Number(d.confidence).toFixed(3);
    document.getElementById('action').textContent = d.action;
    document.getElementById('uptime').textContent = d.uptime_sec + ' s';
    document.getElementById('heap').textContent = (d.free_heap / 1024).toFixed(1) + ' KB';
    document.getElementById('mqtt').textContent = d.mqtt_connected ? 'Connected' : 'Disconnected';
    document.getElementById('mqtt').className = 'value ' + (d.mqtt_connected ? 'on' : 'off');

    var ls = document.getElementById('light-status');
    ls.textContent = d.light_on ? 'ON' : 'OFF';
    ls.className = 'device-status ' + (d.light_on ? 'on' : 'off');

    var fs = document.getElementById('fan-status');
    fs.textContent = d.fan_on ? 'ON' : 'OFF';
    fs.className = 'device-status ' + (d.fan_on ? 'on' : 'off');
  } catch(e) {}
}

setInterval(update, 1000);
update();
</script>
</body>
</html>
)rawliteral";

  return html;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleStatus() {
  String json = "{";
  json += "\"label\":\"" + lastLabel + "\",";
  json += "\"confidence\":" + String(lastConfidence, 4) + ",";
  json += "\"action\":\"" + lastAction + "\",";
  json += "\"light_on\":" + String(lightOn ? "true" : "false") + ",";
  json += "\"fan_on\":" + String(fanOn ? "true" : "false") + ",";
  json += "\"mqtt_connected\":" + String(mqttClient.connected() ? "true" : "false") + ",";
  json += "\"uptime_sec\":" + String(millis() / 1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap());
  json += "}";

  server.send(200, "application/json", json);
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.begin();

  Serial.println("[WEB] Server started");
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  // LED RGB
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setRGB(false, false, false);

  // MOSFET Fan
  pinMode(FAN_MOSFET_PIN, OUTPUT);
  digitalWrite(FAN_MOSFET_PIN, LOW);  // Quạt tắt khi khởi động

  Serial.println();
  Serial.println("===================================");
  Serial.println("ESP32 MQTT KWS Receiver");
  Serial.println("Phone Edge Impulse WASM -> MQTT -> ESP32");
  Serial.println("LED RGB: R=GPIO2, G=GPIO4, B=GPIO16");
  Serial.println("FAN MOSFET: GPIO17");
  Serial.println("===================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WIFI] Connecting");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[WIFI] Connected");
  Serial.print("[WIFI] IP address: ");
  Serial.println(WiFi.localIP());

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  setupWebServer();

  Serial.println("[SYSTEM] Ready - Waiting for MQTT commands...");
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  mqttClient.loop();
  server.handleClient();
}