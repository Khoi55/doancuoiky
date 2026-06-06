#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>

// ================= WIFI =================
const char* WIFI_SSID = "Khoideptrai";
const char* WIFI_PASS = "12345678";

// ================= MQTT =================
// Điện thoại WebAssembly sẽ publish vào topic này.
// ESP32 sẽ subscribe cùng topic.
const char* MQTT_HOST = "broker.emqx.io";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "khoi55/aimangcambien/N22DCDT031/command";

// ================= LED RGB =================
#define LED_R 2
#define LED_G 4
#define LED_B 16

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

void setRGB(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? HIGH : LOW);
  digitalWrite(LED_G, g ? HIGH : LOW);
  digitalWrite(LED_B, b ? HIGH : LOW);
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
    setRGB(false, true, false); // xanh lá
    lastAction = "LIGHT_ON";
  }
  else if (label == "tat_den") {
    lightOn = false;
    setRGB(true, false, false); // đỏ
    lastAction = "LIGHT_OFF";
  }
  else if (label == "bat_quat") {
    setRGB(false, false, true); // xanh dương
    lastAction = "FAN_ON_DEMO_BLUE";
  }
  else if (label == "tat_quat") {
    setRGB(true, false, true); // tím
    lastAction = "FAN_OFF_DEMO_PURPLE";
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

  // An toàn: bỏ qua nếu confidence quá thấp
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
  font-family: Arial, sans-serif;
  background: #f4f6f8;
  margin: 0;
  padding: 20px;
}
.container {
  max-width: 760px;
  margin: auto;
}
.card {
  background: white;
  padding: 18px;
  border-radius: 14px;
  margin-bottom: 16px;
  box-shadow: 0 4px 14px rgba(0,0,0,0.08);
}
.grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
}
.metric {
  background: #f8fafc;
  border-radius: 12px;
  padding: 14px;
}
.label {
  color: #666;
  font-size: 13px;
}
.value {
  font-size: 24px;
  font-weight: bold;
}
</style>
</head>
<body>
<div class="container">
  <h1>ESP32 KWS Receiver Dashboard</h1>
  <p>Nhận kết quả từ điện thoại chạy Edge Impulse WebAssembly qua MQTT.</p>

  <div class="card">
    <h2>Prediction Result</h2>
    <div class="grid">
      <div class="metric">
        <div class="label">Last Label</div>
        <div id="label" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Confidence</div>
        <div id="confidence" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Action</div>
        <div id="action" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Light</div>
        <div id="light" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Uptime</div>
        <div id="uptime" class="value">--</div>
      </div>
      <div class="metric">
        <div class="label">Free Heap</div>
        <div id="heap" class="value">--</div>
      </div>
    </div>
  </div>
</div>

<script>
async function update() {
  const r = await fetch('/api/status');
  const d = await r.json();

  document.getElementById('label').textContent = d.label;
  document.getElementById('confidence').textContent = Number(d.confidence).toFixed(2);
  document.getElementById('action').textContent = d.action;
  document.getElementById('light').textContent = d.light_on ? 'ON' : 'OFF';
  document.getElementById('uptime').textContent = d.uptime_sec + ' s';
  document.getElementById('heap').textContent = d.free_heap;
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

  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);

  setRGB(false, false, false);

  Serial.println();
  Serial.println("===================================");
  Serial.println("ESP32 MQTT KWS Receiver");
  Serial.println("Phone Edge Impulse WASM -> MQTT -> ESP32");
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

  Serial.println("[SYSTEM] Ready");
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }

  mqttClient.loop();
  server.handleClient();
}