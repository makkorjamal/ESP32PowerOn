#include <FS.h>                   // needs to be first
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>
#include <WakeOnLan.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <vector>
#include <SPIFFS.h>
#include <ArduinoJson.h>

#if ARDUINOJSON_VERSION_MAJOR >= 7
  #define JSON_DOC(name) JsonDocument name
#else
  #define JSON_DOC(name) DynamicJsonDocument name(1024)
#endif

WiFiManager wifiManager;
WiFiManagerParameter custom_radio_field;
WiFiManagerParameter custom_dropdown;

String radio_select = "wired";
String mac_saved = "";
String controller_saved = "Xbox";
String controller_other_saved = "";

std::vector<const char*> dropDown = {"Xbox", "Steam", "DualSense", "Other"};
String radioHtml;      
String dropdownHtml;

bool shouldSaveConfig = false;

int deviceCount = 0;
String controllerAddress = "";
String strongestName = "Unknown";
int scanTime = 1;
int strongestRSSI = -100;

const char* MACAddress = "a0:ad:9f:a3:5f:10";
const int      SCAN_TIME_S        = 3;       // BLE scan length
const uint32_t WAKE_TIMEOUT       = 60000;   // give up waiting for the PC's "on" message
const uint32_t COOLDOWN_AFTER_OFF = 2000;   // ignore controller right after shutdown
bool pcOn = false;
String rxLine;
volatile bool controllerFound = false;
BLEScan* pBLEScan;
WiFiUDP UDP;
WakeOnLan WOL(UDP);
enum State { PC_ON, WAIT_CONTROLLER, WAKING };
State state;
uint32_t stateSince = 0;
const int OPTO_PIN = 9;     // D10 (GPIO9)
const int PRESS_MS = 1500; //Was best for my PC you can tune it


class MyAdvertisedDVeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
void onResult(BLEAdvertisedDevice device) {
    deviceCount++;
    int rssi = device.getRSSI();
    if (rssi > strongestRSSI) {
      strongestRSSI = rssi;
      strongestName = device.haveName() ?
        device.getName().c_str() : device.getAddress().toString().c_str();
    }
    if (device.haveName()) {
      String name = device.getName().c_str();
      if (name.indexOf(controller_saved) >=0 || name.indexOf(controller_other_saved) ) {
        controllerFound = true;
        controllerAddress = device.getAddress().toString().c_str();
        BLEDevice::getScan()->stop();
      }
    }
  }
};

void pollSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      rxLine.trim();
      if (rxLine == "on")       pcOn = true;
      else if (rxLine == "off") pcOn = false;
      rxLine = "";
    } else if (rxLine.length() < 32) {
      rxLine += c;
    }
  }
}

void setState(int s) {
  state = (State)s;
  stateSince = millis();
  const char* names[] = {"PC_ON", "WAIT_CONTROLLER", "WAKING"};
  Serial.printf("-> %s\n", names[s]);
}
void WiredWakeMyPC() {
  digitalWrite(OPTO_PIN, HIGH);
  delay(PRESS_MS);
  digitalWrite(OPTO_PIN, LOW);
  Serial.println("Power button pressed.");
}

void WakeOnLanMyPC(String mac_saved) {
  const char *MACAddress = mac_saved.c_str();
  bool sent = false;
  if(mac_saved){
    sent = WOL.sendMagicPacket(MACAddress);
  }
  if (sent) {
    Serial.println("Magic packet sent.");
  }
}

void saveParamsCallback() {
  if (!wifiManager.server) return;
  radio_select           = wifiManager.server->arg("radio_select");
  mac_saved              = wifiManager.server->arg("mac");
  controller_saved       = wifiManager.server->arg("controller");
  controller_other_saved = wifiManager.server->arg("controller_other");
  shouldSaveConfig = true;
  Serial.println("Params saved from portal");
}

String escapeAttr(String s) {
  s.replace("'", "&#39;");
  s.replace("<", "&lt;");
  return s;
}

String buildRadioButtons(const String& saved, const String& mac) {
  bool isWol = (saved == "wol");

  String html = "<br/><label>Wakeup Method:</label> ";

  html += "<input type='radio' name='radio_select' id='r_wired' value='wired'";
  html += isWol ? "" : " checked";
  html += " onclick='document.getElementById(\"mac_box\").style.display=\"none\"'>";
  html += "<label for='r_wired'> Wired</label>";

  html += " | ";

  html += "<input type='radio' name='radio_select' id='r_wol' value='wol'";
  html += isWol ? " checked" : "";
  html += " onclick='document.getElementById(\"mac_box\").style.display=\"block\"'>";
  html += "<label for='r_wol'> WakeOnLan</label>";

  html += "<div id='mac_box' style='display:";
  html += isWol ? "block" : "none";
  html += "'><br/><label for='mac'>PC MAC address:</label><br/>"
          "<input type='text' name='mac' id='mac' maxlength='17' "
          "placeholder='AA:BB:CC:DD:EE:FF' value='";
  html += escapeAttr(mac);
  html += "'></div>";

  return html;
}

String buildDropdownHtml(const String& saved, const String& otherText) {
  bool isOther = (saved == "Other");

  String html = "<br/><label for='controller'>Controller type:</label> ";
  html += "<select name='controller' id='controller' "
          "onchange='document.getElementById(\"other_box\").style.display="
          "(this.value==\"Other\")?\"inline\":\"none\"'>";

  for (const char* item : dropDown) {
    html += "<option value='";
    html += item;
    html += "'";
    if (saved == item) html += " selected";
    html += ">";
    html += item;
    html += "</option>";
  }
  html += "</select> ";

  html += "<input type='text' name='controller_other' id='other_box' "
          "placeholder='Enter name' value='";
  html += escapeAttr(otherText);
  html += "' style='display:";
  html += isOther ? "inline" : "none";
  html += "'>";
  return html;
}

void loadConfig() {
  if (!SPIFFS.exists("/config.json")) return;

  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) return;

  JSON_DOC(json);
  DeserializationError err = deserializeJson(json, configFile);   // parse straight from file
  configFile.close();

  if (err) {
    Serial.println("failed to load json config");
    return;
  }

  radio_select           = json["radio_select"]     | "wired";
  mac_saved              = json["mac"]              | "";
  controller_saved       = json["controller"]       | "Xbox";
  controller_other_saved = json["controller_other"] | "";
  Serial.println("config loaded");
}

void saveConfig() {
  JSON_DOC(json);
  json["radio_select"]     = radio_select;
  json["mac"]              = mac_saved;
  json["controller"]       = controller_saved;
  json["controller_other"] = controller_other_saved;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("failed to open config file for writing");
    return;
  }
  serializeJson(json, configFile);
  configFile.close();
  Serial.println("config saved");
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  if (SPIFFS.begin(true)) {          // true = format if mount fails
    loadConfig();
  } else {
    Serial.println("failed to mount FS");
  }

  radioHtml = buildRadioButtons(radio_select, mac_saved);
  new (&custom_radio_field) WiFiManagerParameter(radioHtml.c_str());
  wifiManager.addParameter(&custom_radio_field);

  dropdownHtml = buildDropdownHtml(controller_saved, controller_other_saved);
  new (&custom_dropdown) WiFiManagerParameter(dropdownHtml.c_str());
  wifiManager.addParameter(&custom_dropdown);

  wifiManager.setSaveParamsCallback(saveParamsCallback);
  wifiManager.setMinimumSignalQuality(30);

  if (!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  Serial.println("connected");

  if (shouldSaveConfig) {
    saveConfig();
  }

  Serial.println("Current values:");
  Serial.println("\tradio_select : " + radio_select);
  Serial.println("\tmac_saved : " + mac_saved);
  Serial.println("\tcontroller_saved : " + controller_saved);
  Serial.println("\tcontroller_other_saved : " + controller_other_saved);

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  WOL.setRepeat(3, 100);
  WOL.calculateBroadcastAddress(WiFi.localIP(), WiFi.subnetMask());

  pinMode(OPTO_PIN, OUTPUT);
  digitalWrite(OPTO_PIN, LOW);
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDVeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  pollSerial();
  setState(pcOn ? PC_ON : WAIT_CONTROLLER);
}

void loop() {

  pollSerial();
  controllerFound = false; // reset before each scan pass
  controllerAddress = "";

  switch (state) {

    case PC_ON:
      // No scanning while the PC is on.
      if (!pcOn) setState(WAIT_CONTROLLER);   // "off" received
      delay(200);
      break;

    case WAIT_CONTROLLER:
      if (pcOn) { setState(PC_ON); break; }   // "on" received (manual power-on)
      if (millis() - stateSince < COOLDOWN_AFTER_OFF) { delay(200); break; }

      pBLEScan->start(scanTime, false);
      pBLEScan->clearResults(); // free scan memory, keeps future scans fast

      if (controllerFound) {
        Serial.println("Controller: FOUND!");
        Serial.println(controllerAddress);
        setState(WAKING);
	if (radio_select.equals("wired")) {
		WiredWakeMyPC();
	} else if (radio_select.equals("wol")) {
		WakeOnLanMyPC(mac_saved);
	} else {
		Serial.println("No correct wake method chosen");
	}
      }
      break;

    case WAKING:
      if (pcOn) { setState(PC_ON); break; }   // PC booted and said "on"
      if (millis() - stateSince > WAKE_TIMEOUT) {
        Serial.println("No 'on' message, back to waiting");
        setState(WAIT_CONTROLLER);
      }
      delay(200);
      break;
  }
}
