// Node 1 – Smart Street Light
// ESP32 Nano + PIR + LED
// Bachelor’s Thesis – Savonia UAS
#include <WiFi.h>
#include <WebServer.h>
#include "painlessMesh.h"

// ================== PHASE 1: CONFIG (WEB) ==================
#define CONFIG_AP_SSID      "SmartMeshConfig"
#define CONFIG_AP_PASSWORD  "12345678"   // ≥ 8 chars

WebServer server(80);

// ================== PHASE 2: MESH ==========================
#define MESH_PREFIX         "SmartStreetMesh"
#define MESH_PASSWORD       "streetlightPass"
#define MESH_PORT           5555

Scheduler userScheduler;
painlessMesh mesh;

// ================== HARDWARE PINS ==========================
// Adjust to your wiring as needed
const int PIR_PIN = 7;
const int LED_PIN = 4;

// ================== NODE INFO ==============================
const uint8_t MY_NODE_INDEX = 1; // This board is Node 1 (gateway)

// ================== STATE / PARAMS =========================
enum RunMode { MODE_CONFIG, MODE_MESH };
RunMode currentMode = MODE_CONFIG;
bool    requestMeshMode = false;       // set true after button press
uint32_t ledOnTimeMs = 2000;           // configured in web; used in mesh

// For one-time broadcast after mesh starts
bool cfgSent = false;
unsigned long meshStartMillis = 0;

// ================== LED / MOTION LOGIC =====================
bool ledIsOn = false;
unsigned long ledOffTime = 0;

void setLED(bool on) {
  ledIsOn = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  if (on) ledOffTime = millis() + ledOnTimeMs;
}

void processMotion(uint8_t S) {
  uint8_t ME = MY_NODE_INDEX;
  bool turnOn = false;

  // Streetlight pattern
  switch (S) {
    case 1: if (ME == 1 || ME == 2) turnOn = true; break;
    case 2: if (ME == 1 || ME == 2 || ME == 3) turnOn = true; break;
    case 3: if (ME == 2 || ME == 3 || ME == 4) turnOn = true; break;
    case 4: if (ME == 3 || ME == 4) turnOn = true; break;
  }
  setLED(turnOn);
}

// ================== MESH CALLBACKS =========================
void receivedCallback(uint32_t from, String &msg) {
  Serial.printf("Mesh RX from %u: %s\n", from, msg.c_str());

  if (msg.startsWith("M:")) {
    uint8_t src = msg.substring(2).toInt();
    processMotion(src);
  }
  else if (msg.startsWith("CFG:DELAY:")) {
    uint32_t v = msg.substring(10).toInt();
    if (v > 0 && v < 60000) {
      ledOnTimeMs = v;
      Serial.printf("Node %u updated ledOnTimeMs from mesh = %u ms\n",
                    MY_NODE_INDEX, ledOnTimeMs);
    }
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Serial.printf("Node %u: New connection %u\n", MY_NODE_INDEX, nodeId);
}
void changedConnectionCallback() {
  Serial.printf("Node %u: Connections changed\n", MY_NODE_INDEX);
}
void nodeTimeAdjustedCallback(int32_t offset) {
  Serial.printf("Node %u: Time adjusted %d\n", MY_NODE_INDEX, offset);
}

// ================== WEB PAGE (CONFIG MODE) =================
String htmlPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mesh Delay Control</title>
<style>
  body{font-family:Arial, sans-serif; max-width:420px; margin:30px auto;}
  h2{margin-bottom:10px}
  input,button{width:100%; padding:10px; margin-top:8px; box-sizing:border-box}
  button{cursor:pointer}
  small{color:#555}
</style>
</head>
<body>
  <h2>Mesh Delay Control</h2>
  <form action="/" method="GET">
    <label>LED ON time for all nodes (ms):</label>
    <input name="delay" type="number" min="100" max="10000" step="100" required>
    <button type="submit">Set &amp; Start Mesh</button>
  </form>
  <small>After you press the button, this Wi-Fi will turn off and the device will switch to mesh mode.</small>
</body>
</html>
)rawliteral";
  return page;
}

void handleRoot() {
  if (server.hasArg("delay")) {
    uint32_t v = server.arg("delay").toInt();
    if (v > 0 && v < 60000) {
      ledOnTimeMs = v;
      Serial.print("New delay received from web = ");
      Serial.println(ledOnTimeMs);

      // Acknowledge to user, then switch modes
      String resp = "<html><body><h2>Delay set to " + String(ledOnTimeMs) +
                    " ms</h2><p>Switching to mesh mode... this Wi-Fi will disappear.</p></body></html>";
      server.send(200, "text/html", resp);

      requestMeshMode = true;   // handled in loop()
      return;
    }
  }
  server.send(200, "text/html", htmlPage());
}

// Simple status endpoint (config mode only): http://192.168.4.1/status
void handleStatus() {
  String s = "mode=config&delay_ms=" + String(ledOnTimeMs);
  server.send(200, "text/plain", s);
}

// ================== MODE CONTROL ===========================
void startConfigMode() {
  currentMode = MODE_CONFIG;
  requestMeshMode = false;
  cfgSent = false;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);

  Serial.println();
  Serial.println("=== CONFIG MODE ===");
  Serial.print("Connect to Wi-Fi SSID: "); Serial.println(CONFIG_AP_SSID);
  Serial.print("Password: ");             Serial.println(CONFIG_AP_PASSWORD);
  Serial.print("Open in browser: http://"); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.begin();
  Serial.println("Web server started (config mode)");

  // Quick LED blink to show we're alive
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); delay(120); digitalWrite(LED_PIN, LOW);
}

void startMeshMode() {
  Serial.println();
  Serial.println("Switching to MESH MODE...");

  server.stop();
  delay(100);
  WiFi.softAPdisconnect(true);
  delay(200);
  WiFi.mode(WIFI_AP_STA); // mesh manages Wi-Fi from now

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  meshStartMillis = millis();
  cfgSent = false;
  currentMode = MODE_MESH;

  Serial.println("Mesh started. Now in MESH MODE.");
  Serial.print("Using ledOnTimeMs = "); Serial.println(ledOnTimeMs);
}

// ================== SETUP / LOOP ===========================
void setup() {
  Serial.begin(115200);
  // Give Serial a moment so boot logs are visible if monitor is open
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(10); }
  delay(300);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  startConfigMode();
}

void loop() {
  if (currentMode == MODE_CONFIG) {
    server.handleClient();

    if (requestMeshMode) {
      startMeshMode();
    }
  } else { // MODE_MESH
    mesh.update();

    // Broadcast the configured delay once, ~5s after mesh starts
    if (!cfgSent && (millis() - meshStartMillis > 5000)) {
      String msg = "CFG:DELAY:" + String(ledOnTimeMs);
      mesh.sendBroadcast(msg);
      Serial.printf("Broadcasting initial config: %s\n", msg.c_str());
      cfgSent = true;
    }

    // Local PIR → broadcast motion + run pattern
    if (digitalRead(PIR_PIN) == HIGH) {
      String msg = "M:" + String(MY_NODE_INDEX);
      mesh.sendBroadcast(msg);
      processMotion(MY_NODE_INDEX);
      delay(200);
    }

    // Auto-off LED
    if (ledIsOn && millis() > ledOffTime) {
      setLED(false);
    }
  }
}
