// Other Node – Smart Street Light
// ESP32 Nano + PIR + LED
// Bachelor’s Thesis – Savonia UAS
#include "painlessMesh.h"

#define MESH_PREFIX   "SmartStreetMesh"
#define MESH_PASSWORD "streetlightPass"
#define MESH_PORT     5555

Scheduler userScheduler;
painlessMesh mesh;

const int PIR_PIN = 7;
const int LED_PIN = 4;

// <<< CHANGE PER BOARD: 2, 3, or 4 >>>
const uint8_t MY_NODE_INDEX = 2;

// runtime delay (updated from Node 1)
uint32_t ledOnTimeMs = 2000;

bool ledIsOn = false;
unsigned long ledOffTime = 0;

void setLED(bool on) {
  ledIsOn = on;
  digitalWrite(LED_PIN, on ? HIGH : LOW);
  if (on) ledOffTime = millis() + ledOnTimeMs;
}

void processMotion(uint8_t S) {
  uint8_t ME = MY_NODE_INDEX;
  bool on = false;
  switch (S) {
    case 1: if (ME == 1 || ME == 2) on = true; break;
    case 2: if (ME == 1 || ME == 2 || ME == 3) on = true; break;
    case 3: if (ME == 2 || ME == 3 || ME == 4) on = true; break;
    case 4: if (ME == 3 || ME == 4) on = true; break;
  }
  setLED(on);
}

void receivedCallback(uint32_t from, String &msg) {
  if (msg.startsWith("M:")) {
    uint8_t src = msg.substring(2).toInt();
    processMotion(src);
    Serial.printf("Node %u received motion from Node %u\n", MY_NODE_INDEX, src);
  } else if (msg.startsWith("CFG:DELAY:")) {
    uint32_t v = msg.substring(10).toInt();
    if (v > 0 && v < 60000) {
      ledOnTimeMs = v;
      Serial.printf("Node %u updated ledOnTimeMs = %u ms\n", MY_NODE_INDEX, ledOnTimeMs);
    }
  }
}

void newConnectionCallback(uint32_t id){ Serial.printf("Node %u: New connection %u\n", MY_NODE_INDEX, id); }
void changedConnectionCallback(){ Serial.printf("Node %u: Connections changed\n", MY_NODE_INDEX); }
void nodeTimeAdjustedCallback(int32_t offset){ Serial.printf("Node %u: Time adjusted %d\n", MY_NODE_INDEX, offset); }

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  Serial.printf("Node %u ready (default ledOnTimeMs=%u ms)\n", MY_NODE_INDEX, ledOnTimeMs);
}

void loop() {
  mesh.update();

  // Local PIR trigger
  if (digitalRead(PIR_PIN) == HIGH) {
    mesh.sendBroadcast("M:" + String(MY_NODE_INDEX));
    processMotion(MY_NODE_INDEX);
    delay(200);
  }

  // Auto OFF after timer expires
  if (ledIsOn && millis() > ledOffTime) setLED(false);
}
