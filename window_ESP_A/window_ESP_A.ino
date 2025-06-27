#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <espnow.h>
#include <ArduinoJson.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <EEPROM.h>

// ==================== SINRIC PRO - PLACEHOLDERY ====================
#define APP_KEY         "xxx"
#define APP_SECRET      "xxx"
#define SWITCH_ID       "xxx"
#define BAUD_RATE       115200
#define EEPROM_SIZE     64
#define EEPROM_ADDR     0

String lastCommand = "idle";
uint8_t wifiChannel;

// ==================== EEPROM ====================
void saveCommandToEEPROM(String cmd) {
  EEPROM.begin(EEPROM_SIZE);
  int len = cmd.length();
  EEPROM.write(EEPROM_ADDR, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(EEPROM_ADDR + 1 + i, cmd[i]);
  }
  EEPROM.commit();
  EEPROM.end();
}

String loadCommandFromEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  int len = EEPROM.read(EEPROM_ADDR);
  String cmd = "";
  for (int i = 0; i < len; i++) {
    char c = EEPROM.read(EEPROM_ADDR + 1 + i);
    cmd += c;
  }
  EEPROM.end();
  return cmd.length() == 0 ? "idle" : cmd;
}

// ==================== SINRIC PRO ====================
bool onPowerState(const String &deviceId, bool &state) {
  lastCommand = state ? "open" : "close";
  saveCommandToEEPROM(lastCommand);
  Serial.printf("[SinricPro] Nowe polecenie: %s\n", lastCommand.c_str());
  return true;
}

void setupSinric() {
  SinricProSwitch &mySwitch = SinricPro[SWITCH_ID];
  mySwitch.onPowerState(onPowerState);
  SinricPro.begin(APP_KEY, APP_SECRET);
  SinricPro.restoreDeviceStates(true);
}

// ==================== ESP-NOW ====================
void onDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.printf("[ESP-NOW] Status wysłania: %s\n", sendStatus == 0 ? "SUKCES" : "BŁĄD");
  if (sendStatus == 0) {
    lastCommand = "idle";
    saveCommandToEEPROM(lastCommand);
    Serial.println("[SYSTEM] Zresetowano komendę na 'idle'");
  }

}

void onDataRecv(uint8_t *mac, uint8_t *data, uint8_t len) {
  Serial.printf("\n[ESP-NOW] Odebrano %d bajtów od %02X:%02X:%02X:%02X:%02X:%02X\n",
                len, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Parsowanie danych
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, data, len);
  
  if (error) {
    Serial.printf("[ESP-NOW] Błąd parsowania JSON: %s\n", error.c_str());
    return;
  }

  const char* type = doc["type"];
  int id = doc["id"];

  if (String(type) == "WAKEUP") {
    float voltage = doc["vbat"] | 0.0;
    Serial.printf("[ESP-NOW] WAKEUP od id=%d, vbat=%.2f V\n", id, voltage);

    // Przygotuj odpowiedź
    DynamicJsonDocument responseDoc(128);
    responseDoc["type"] = "COMMAND";
    responseDoc["cmd"] = lastCommand;
    responseDoc["id"] = id;
    
    char responseStr[128];
    size_t responseLen = serializeJson(responseDoc, responseStr);
    const int maxRetries = 3;
    bool sentOk = false;

    for (int i = 0; i < maxRetries; i++) {
      uint8_t result = esp_now_send(mac, (uint8_t *)responseStr, responseLen);
      if (result == 0) {
        sentOk = true;
        break;
      } else {
        Serial.printf("[ESP-NOW] Błąd wysyłania (próba %d), kod: %d\n", i + 1, result);
        delay(50); // mała przerwa przed kolejną próbą
      }
    }
    if (sentOk) {
      Serial.printf("[ESP-NOW] Wysłano COMMAND: %s\n", responseStr);
    } else {
      Serial.println("[ESP-NOW] Nie udało się wysłać COMMAND po 3 próbach.");
    }
    lastCommand = "idle";
  }
}

void initESPNow() {
  if (esp_now_init() != 0) {
    Serial.println("[ESP-NOW] Błąd inicjalizacji!");
    ESP.restart();
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

  // Dodaj peer (broadcast)
  uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  if (esp_now_add_peer(broadcastAddr, ESP_NOW_ROLE_COMBO, wifiChannel, NULL, 0) != 0) {
    Serial.println("[ESP-NOW] Błąd dodawania peera!");
    ESP.restart();
  }
}

void setup() {
  Serial.begin(BAUD_RATE);
  delay(1000);
  Serial.println("\n[INIT] Uruchamianie ESP A...");

  WiFiManager wifiManager;
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.autoConnect("ESP_A_Config");
  
  wifiChannel = WiFi.channel();
  Serial.printf("[WiFi] Połączono na kanale: %d\n", wifiChannel);

  lastCommand = loadCommandFromEEPROM();
  Serial.printf("[SYSTEM] Ostatnia komenda: %s\n", lastCommand.c_str());

  initESPNow();
  setupSinric();
  Serial.println("[SYSTEM] ESP A gotowe");
}

void loop() {
  SinricPro.handle();
}