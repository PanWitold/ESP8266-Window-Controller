#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <DHT.h>
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <WiFiManager.h>
#include <espnow.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>

#define DHTTYPE DHT22
#define DHTPIN 4            //D2
#define RELAY_POWER 12      //D6
#define RELAY_DIRECTION 13  //D7
#define LED_PIN 15          //D8 
#define SERVER_SWITCH_PIN 14//D5

#define EEPROM_SIZE 65
#define DEVICE_NUM_ID 1
#define ESPNOW_WAIT_MS 200

#define R1 47000.0 // 47kΩ
#define R2 10000.0 // 10kΩ
#define ANALOG_VREF 3.3
#define BATTERY_CHECK_INTERVAL_SEC 1800  // 30 minutes
#define BATTERY_CRITICAL_INTERVAL_SEC 10800  // 3 hours


const char* DEVICE_ID = "xxx";
const char* APP_KEY = "xxx";
const char* APP_SECRET = "xxx";
SinricProSwitch* mySwitch = nullptr;
bool firstSinricCommand = true;
uint8_t espA_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

DHT dht(DHTPIN, DHTTYPE);
ESP8266WebServer server(80);

int wilgProgOpen = 70;
int wilgProgClose = 60;
int czasOtwarcia = 4000;
unsigned int sensorIntervalSec = 30;
bool oknoOtwarte = true;
bool serverEnabled = true;
bool ledBlinkOnWake = true;
String lastReceivedCommand = "idle";
bool commandReceived = false;
String receivedCmd = "";
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

float wilgotnosc = 0;
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 30000;
float batteryVoltage = 0.0;
String batteryStatus = "Nieznany";
int wifiChannel = 1;

enum TrybPracy { AUTO, MANUAL, SENSOR_ONLY, SINRIC_ONLY, HYBRID_AUTO };
TrybPracy tryb = AUTO;


void setupRelays() {
  pinMode(RELAY_POWER, OUTPUT);
  pinMode(RELAY_DIRECTION, OUTPUT);
  pinMode(SERVER_SWITCH_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_POWER, HIGH);
  digitalWrite(RELAY_DIRECTION, HIGH);
}

void closeWindow() {
  if (!oknoOtwarte) {
    Serial.println("Okno już jest zamknięte – pomijam zamykanie.");
    return;
  }
  Serial.println("Zamykam okno...");
  digitalWrite(RELAY_DIRECTION, LOW);
  delay(czasOtwarcia);
  digitalWrite(RELAY_POWER, HIGH);
  digitalWrite(RELAY_DIRECTION, HIGH);
  Serial.println("Zamknięte");
  oknoOtwarte = false;
  savePreferences();
  mySwitch->sendPowerStateEvent(false); //to sinric pro
}

void openWindow() {
  if (oknoOtwarte) {
    Serial.println("Okno już jest otwarte – pomijam otwieranie.");
    return;
  }
  Serial.println("Otwieram okno...");
  digitalWrite(RELAY_POWER, LOW);
  delay(czasOtwarcia);
  digitalWrite(RELAY_POWER, HIGH);
  digitalWrite(RELAY_DIRECTION, HIGH);
  Serial.println("Otwarte.");
  oknoOtwarte = true;
  savePreferences();
  mySwitch->sendPowerStateEvent(true); //to sinric pro
}

void loadPreferences() {
  EEPROM.begin(EEPROM_SIZE);
  czasOtwarcia = EEPROM.read(1) * 100;
  oknoOtwarte = EEPROM.read(3) == 1;
  sensorIntervalSec = EEPROM.read(4);
  wilgProgOpen = EEPROM.read(5);
  wilgProgClose = EEPROM.read(6);
  tryb = (TrybPracy)EEPROM.read(7);
  ledBlinkOnWake = EEPROM.read(8) == 1;
  wifiChannel = EEPROM.read(9);
  EEPROM.end();

  if (sensorIntervalSec < 1 || sensorIntervalSec > 600) sensorIntervalSec = 30;
  if (czasOtwarcia <= 0 || czasOtwarcia > 80000) czasOtwarcia = 4000;
  if (tryb < 0 || tryb > HYBRID_AUTO) tryb = AUTO;
  if (wifiChannel < 1 || wifiChannel > 13) wifiChannel = 1;
}

void savePreferences() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(1, czasOtwarcia / 100);
  EEPROM.write(3, oknoOtwarte ? 1 : 0);
  EEPROM.write(4, sensorIntervalSec);
  EEPROM.write(5, wilgProgOpen);
  EEPROM.write(6, wilgProgClose);
  EEPROM.write(7, tryb);
  EEPROM.write(8, ledBlinkOnWake ? 1 : 0);
  EEPROM.write(9, wifiChannel);
  EEPROM.commit();
  EEPROM.end();
}

void sendWakeup() {
  StaticJsonDocument<64> doc;
  doc["type"] = "WAKEUP";
  doc["id"] = DEVICE_ID;
  doc["vbat"] = measureBatteryVoltage();

  char buffer[96];
  serializeJson(doc, buffer);

  Serial.printf("[TX] Wysyłam WAKEUP (id: %d, vbat: %.2f)\n", DEVICE_ID, doc["vbat"].as<float>());
  esp_now_send(espA_mac, (uint8_t *)buffer, strlen(buffer));
  Serial.println("Wysłano WAKEUP");
}

void initESPNow() {
  WiFi.mode(WIFI_STA);
  wifi_set_channel(wifiChannel);

  if (esp_now_init() != 0) {
    Serial.println("Błąd inicjalizacji ESP-NOW");
    return;
  }
  int32_t channel = WiFi.channel();

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataRecv);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_COMBO, channel, NULL, 0);
  sendWakeup();
}

void onDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
  Serial.printf("Odebrano %d bajtów od %02X:%02X:%02X:%02X:%02X:%02X\n", 
               len, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  StaticJsonDocument<100> doc;
  DeserializationError error = deserializeJson(doc, incomingData, len);
  
  if (error) {
    Serial.print("Błąd parsowania: ");
    Serial.println(error.c_str());
    return;
  }

  if (doc["type"] == "COMMAND") {
    receivedCmd = doc["cmd"].as<String>();
    commandReceived = true;
    Serial.println("Odebrano CMD: " + receivedCmd);
  }
}

float measureBatteryVoltage() {
  int raw = analogRead(A0);
  float vOut = (raw / 1023.0) * ANALOG_VREF;
  float vBat = vOut * ((R1 + R2) / R2);
  return vBat;
}

String getBatteryStatus(float voltage) {
  if (voltage > 4.0) return "Naładowana";
  else if (voltage > 3.5) return "W trakcie cyklu";
  else if (voltage > 3.2) return "Niski poziom baterii";
  else if (voltage > 3.0 && voltage < 1.5) return "Krytyczny poziom baterii!";
  else return "Błąd odczytu stanu baterii!";
}

void protectBattery(float voltage){
  int retry = 0;
  while(voltage < 1.5 || voltage > 4.5){
    delay(1000);
    voltage = measureBatteryVoltage();
    retry ++;
    if (retry > 5) return;
  }
  Serial.printf("Napięcie baterii: %.2f V\n", voltage);
  if (voltage < 2.8) {
    Serial.println("Krytycznie niski poziom baterii – sen na 3h");
    ESP.deepSleep(BATTERY_CRITICAL_INTERVAL_SEC * 1e6);
  } else if (voltage < 3.1) {
    Serial.println("Niski poziom baterii – sen na 30 min");
    ESP.deepSleep(BATTERY_CHECK_INTERVAL_SEC * 1e6);
    }
  }

void setupWebConfig() {
  server.on("/", HTTP_GET, []() {
    String trybText = "";
    batteryStatus = getBatteryStatus(batteryVoltage);
    if (tryb == AUTO) trybText = "AUTO";
    else if (tryb == MANUAL) trybText = "MANUAL";
    else if (tryb == SENSOR_ONLY) trybText = "Tylko czujnik";
    else if (tryb == SINRIC_ONLY) trybText = "ESP A+B - Manual";
    else if (tryb == HYBRID_AUTO) trybText = "ESP A+B - Auto";

    String html = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
    html += "<style>body{font-family:Arial;margin:20px;}input,select{padding:5px;margin:5px 0;}</style></head>";
    html += "<body><h2>Konfiguracja Okna</h2>";
    html += "<form action=\"/save\" method=\"POST\">";
    html += "Próg OTWARCIA (%): <input type='number' name='open' min='1' max='100' value='" + String(wilgProgOpen) + "'><br>";
    html += "Próg ZAMKNIĘCIA (%): <input type='number' name='close' min='1' max='100' value='" + String(wilgProgClose) + "'><br>";
    html += "Interwał odczytu (s): <input type='number' name='interval' min='1' max='600' value='" + String(sensorIntervalSec) + "'><br>";
    html += "Czas otwarcia (s): <input type='number' name='czas' min='1' max='40' value='" + String(czasOtwarcia / 1000) + "'><br>";
    html += "Tryb: <select name='tryb'>";
    html += String("<option value='AUTO' ") + (tryb == AUTO ? "selected" : "") + ">Auto</option>";
    html += String("<option value='MANUAL' ") + (tryb == MANUAL ? "selected" : "") + ">Manual</option>";
    html += String("<option value='SENSOR_ONLY' ") + (tryb == SENSOR_ONLY ? "selected" : "") + ">Tylko czujnik</option>";
    html += String("<option value='SINRIC_ONLY' ") + (tryb == SINRIC_ONLY ? "selected" : "") + ">ESP A+B - Manual</option>";
    html += String("<option value='HYBRID_AUTO' ") + (tryb == HYBRID_AUTO ? "selected" : "") + ">ESP A+B - Auto</option>";
    html += "</select><br>";
    html += "<label><input type='checkbox' name='ledblink' ";
    if (ledBlinkOnWake) html += "checked";
    html += ">Miganie diody przy wybudzeniu (tylko w trybie Tylko czujnik)</label><br>";
    html += "<input type='submit' value='Zapisz'>";
    html += "</form>";
    html += "<p>Aktualny tryb: " + trybText + "<br>";


    html += "Aktualny stan okna: " + String(oknoOtwarte ? "Otwarte" : "Zamknięte") + "<br>";  
    html += "Napięcie baterii: " + String(batteryVoltage, 2) + " V<br>";
    html += "Status baterii: " + batteryStatus + "<br>";
    html += "Aktualna wilgotność: " + String(wilgotnosc, 1) + "%, temperatura: " + String(dht.readTemperature(), 1) + " C</p>";
    html += "<p>Próg otwarcia i zamknięcia należy ustawić uwzględniając histerezę –  10-20%</p>";
    html += "<p>TRYBY OPIS:<br>  -AUTO: czujnik wilgotności + aplikacja Google Home<br>  -MANUAL: tylko aplikacja Google Home<br>  -TYLKO CZUJNIK: tryb oszczędności baterii - otwieranie/zamykanie tylko na podstawie wilgotności - w międzyczasie uC uśpiony<br>";
    html += "  -ESP A+B - Manual: wymagane ESP A(komunikacyjne) - tylko aplikacja Google Home<br>  -ESP A+B - Auto: wymagane ESP A(komunikacyjne)   -czujnik wilgotności + aplikacja Google Home</p>";
    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  });

  server.on("/save", HTTP_POST, []() {
    //if (server.hasArg("prog")) wilgotnoscProg = server.arg("prog").toInt();
    if (server.hasArg("open")) wilgProgOpen = server.arg("open").toInt();
    if (server.hasArg("close")) wilgProgClose = server.arg("close").toInt();
    if (server.hasArg("interval")) sensorIntervalSec = server.arg("interval").toInt();
    if (server.hasArg("czas")) czasOtwarcia = server.arg("czas").toInt() * 1000;
    if (server.hasArg("tryb")) {
      String trybStr = server.arg("tryb");
      if (trybStr == "AUTO") tryb = AUTO;
      else if (trybStr == "MANUAL") tryb = MANUAL;
      else if (trybStr == "SENSOR_ONLY") tryb = SENSOR_ONLY;
      else if (trybStr == "SINRIC_ONLY") tryb = SINRIC_ONLY;
      else if (trybStr == "HYBRID_AUTO") tryb = HYBRID_AUTO;
      ledBlinkOnWake = server.hasArg("ledblink");
    }

    savePreferences();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/toggle", HTTP_GET, []() {
  toggleManualMode();
  server.sendHeader("Location", "/");
  server.send(303);
  Serial.print("Panel ustawień dostępny pod adresem: http://");
  });

  server.begin();
  Serial.println("HTTP server started");
  digitalWrite(LED_PIN, HIGH);
}

bool onPowerState(const String &deviceId, bool &state) {
  if (firstSinricCommand) {
    firstSinricCommand = false;
    if (mySwitch) {
      mySwitch->sendPowerStateEvent(oknoOtwarte);
      Serial.println("Zignorowano pierwsze polecenie Sinric, wysłano aktualny stan okna.");
    }
    return false;
  }

  if (tryb == SENSOR_ONLY) {
    Serial.println("Tryb SENSOR_ONLY – polecenia z Sinric ignorowane.");
    return true;
  }

  Serial.println("Polecenie z Google Home: " + String(state ? "OTWÓRZ" : "ZAMKNIJ"));
  if (state) openWindow(); else closeWindow();
  return true;
}

void toggleManualMode() {
  savePreferences();  
  if ((tryb != AUTO) && mySwitch) {
    mySwitch->sendPowerStateEvent(false); // Wysyłamy stan 'off' do SinricPro
  }
}

void setupSinricPro() {
  SinricProSwitch& sw = SinricPro[DEVICE_ID];
  sw.onPowerState(onPowerState);
  SinricPro.begin(APP_KEY, APP_SECRET);
  SinricPro.restoreDeviceStates(true);
  mySwitch = &sw;
}

void handleWakeupAndCommand() {
  Serial.println("Tryb SINRIC_ONLY/HYBRID_AUTO + przycisk wifi wyłączony – odbieranie sygnałów z ESP A");
  digitalWrite(LED_PIN, LOW);
  if (ledBlinkOnWake) digitalWrite(LED_PIN, HIGH);
  initESPNow();
  delay(200);
  //digitalWrite(LED_PIN, LOW);
  Serial.printf("Kanał WiFi: %d\n", wifiChannel);

  unsigned long start = millis();
  unsigned long lastSend = 0;
  unsigned long lastBlink = 0;
  const unsigned long timeout = 30000; //30 second
  bool blinkActive = false;
  bool ledState = false;

  while (!commandReceived && (millis() - start < timeout)) {
    unsigned long now = millis();
    if (now - lastSend > 1000) {
      Serial.println("Wysyłam WAKEUP...");
      sendWakeup();
      lastSend = now;
    }
    if (!blinkActive && now - start > 5000) {
      blinkActive = true;
      Serial.println("Brak odpowiedzi po 5s – miganie LED");
    }

    if (blinkActive && now - lastBlink > 1000) {
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      Serial.println("miganie LED");
      lastBlink = now;

    }
    if (digitalRead(SERVER_SWITCH_PIN) == LOW) {
      Serial.println("Wciśnięto przycisk SERVER – przerywam oczekiwanie na COMMAND");
      return;
    }
    yield();
  }

  digitalWrite(LED_PIN, LOW);
  if (commandReceived) Serial.println("Otrzymano odpowiedź COMMAND");

  if (receivedCmd == "open") {
    openWindow();
  } else if (receivedCmd == "close") {
    closeWindow();
  } else if (receivedCmd == "idle") {
    if (tryb == HYBRID_AUTO) {
      wilgotnosc = dht.readHumidity();
      Serial.printf("HYBRID: Wilgotność: %.1f%% \n", wilgotnosc);
      if (!oknoOtwarte && wilgotnosc > wilgProgOpen) {
        openWindow();
      } else if (oknoOtwarte && wilgotnosc < wilgProgClose) {
        closeWindow();
      } else {
        Serial.println("HYBRID: Brak akcji");
      }
    } else {
      Serial.println("SINRIC_ONLY: idle – brak działania");
    }
  }
  Serial.println("Zasypiam ponownie...");
  delay(500);
  ESP.deepSleep(sensorIntervalSec * 1e6);
}

void setup() {
  Serial.begin(115200);
  setupRelays();
  loadPreferences();
  if(digitalRead(SERVER_SWITCH_PIN) == HIGH){
    protectBattery(measureBatteryVoltage());
  }

  if (tryb == SENSOR_ONLY && digitalRead(SERVER_SWITCH_PIN) == HIGH) {
    Serial.println("Tryb SENSOR_ONLY + przycisk wifi wyłączony – działanie tylko z czujnikiem + deep sleep");
    dht.begin();
    delay(2000);
    float h = dht.readHumidity();
    Serial.printf("Odczyt wilgotności: %.1f%%\n", h);
    if (!oknoOtwarte && h >= wilgProgOpen) {
      openWindow();
    } else if (oknoOtwarte && h <= wilgProgClose) {
      closeWindow();
    }
    Serial.printf("Uśpienie na %d sekund...\n", sensorIntervalSec);
    if (ledBlinkOnWake) digitalWrite(LED_PIN, HIGH);
    delay(500);
    digitalWrite(LED_PIN, LOW);
    ESP.deepSleep(sensorIntervalSec * 1e6);
  }

  if ((tryb == SINRIC_ONLY || tryb == HYBRID_AUTO) && (digitalRead(SERVER_SWITCH_PIN) == HIGH)) {
    handleWakeupAndCommand();
  }

  WiFiManager wifiManager;
  wifiManager.autoConnect("ESP B (wykonawcze)");
  Serial.println("Połączono z WiFi");
  Serial.println(WiFi.localIP());

  int currentChannel = WiFi.channel();
  Serial.printf("Obecny kanał WiFi: %d\n", currentChannel);
  if (currentChannel != wifiChannel) {
    Serial.printf("Zmieniono kanał WiFi z %d na %d, zapisuję...\n", wifiChannel, currentChannel);
    wifiChannel = currentChannel;
    savePreferences();
  }
  dht.begin();
  setupWebConfig();
  setupSinricPro();
}

void loop() {
  SinricPro.handle();
  server.handleClient();

  unsigned long now = millis();
  static unsigned long lastSwitchCheck = 0;

  if ((tryb == SINRIC_ONLY || tryb == HYBRID_AUTO) && (digitalRead(SERVER_SWITCH_PIN) == HIGH)) {
    handleWakeupAndCommand();
  }

  if (millis() - lastSwitchCheck > 1000) {
    lastSwitchCheck = millis();
    bool switchState = digitalRead(SERVER_SWITCH_PIN) == LOW;
    if (switchState != serverEnabled) {
      serverEnabled = switchState;
      if (serverEnabled) {
        server.begin();
        digitalWrite(LED_PIN, HIGH);
        Serial.println("Web serwer WŁĄCZONY");
      } else {
        server.stop();
        digitalWrite(LED_PIN, LOW);
        Serial.println("Web serwer WYŁĄCZONY");
      }
    }
  }

  if (now - lastSensorRead > sensorIntervalSec * 1000UL) {
    lastSensorRead = now;
    wilgotnosc = dht.readHumidity();
    batteryVoltage = measureBatteryVoltage();
    Serial.printf("Wilgotność: %.1f%% ", wilgotnosc);
    Serial.printf("Bateria: %.1f% \n", batteryVoltage);
    Serial.printf("WiFi status: %d, Server enabled: %d\n", WiFi.status(), serverEnabled);

    
    if (tryb == AUTO) {
      if (!oknoOtwarte && (wilgotnosc > wilgProgOpen)) {
        Serial.println("AUTO: Wilgotność przekroczona – otwieram okno");
        openWindow();
      } else if (oknoOtwarte && (wilgotnosc < wilgProgClose)) {
        Serial.println("AUTO: Wilgotność spadła – zamykam okno");
        closeWindow();
      }
    }
  }
}
