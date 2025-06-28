## ESP8266-Window-Controller
ESP8266 Smart Window Controller (ESP-NOW + Sinric Pro)
System automatycznego sterowania oknem łazienkowym zintegrowany z Google Home i Sinric Pro. Wykorzystuje dwa moduły ESP8266: jeden stale zasilany (ESP A) i drugi bateryjny (ESP B).

![ESP window](https://github.com/user-attachments/assets/5f2a8118-f5f7-4825-8087-563ee904e15c)


## Cel projektu

Stworzenie hybrydowego, energooszczędnego systemu zdalnego otwierania i zamykania okna, który:
- współpracuje z Google Home (przez Sinric Pro),
- wykorzystuje ESP-NOW do komunikacji między modułami ESP,
- minimalizuje zużycie energii (ESP B w deep sleep przez większość czasu),
- pozwala na łatwą wymianę modułów ESP B bez zmiany kodu ESP A.

## Architektura systemu

Google Home
↓
Sinric Pro Cloud
↓
ESP A (zasilany stale)
↕ ESP-NOW
ESP B (zasilany bateryjnie)

- **ESP A**: komunikuje się z chmurą (Sinric Pro), zapamiętuje ostatnie polecenie, odpowiada ESP B.
- **ESP B**: wybudza się cyklicznie, prosi o polecenie, wykonuje i zasypia.

## 🔌 Wymagania sprzętowe

- 2 × ESP8266 (np. NodeMCU, D1 Mini)
- Siłownik liniowy 12V (z krańcówkami)
- 2 × przekaźnik do sterowania siłownikiem
- Akumulator (np. 1S3P 18650)
- Moduł step-up 3.7V → 12V (dla siłownika)
- panel słoneczny (np. 30W 5V)
- układ ładujący baterie 18650 z panelu (np. J5019)
- Sinric Pro account (darmowe)

## ⚙️ Działanie

- **ESP A (komunikacyjny):**
  - Łączy się z WiFi przez WiFiManager
  - Obsługuje komendy z Google Home (Sinric Pro)
  - Reaguje na broadcast `"WAKEUP"` od ESP B
  - Odpowiada `"COMMAND"` z zapamiętanym stanem ("open", "close", "idle")

- **ESP B (wykonawczy):**
  - Co X sekund wybudza się z deep sleep
  - Wysyła broadcast `"WAKEUP"` (ESP-NOW)
  - Odbiera `"COMMAND"` i wykonuje akcję
  - Zasypia z powrotem

### 💬 Komunikacja (JSON)

**ESP B → ESP A:**
{ "type": "WAKEUP", "id": 1 }

**ESP A → ESP B:**
{ "type": "COMMAND", "cmd": "open", "id": 1 }

---

### 6. 🔧 Konfiguracja i kompilacja
```markdown
## 🔧 Konfiguracja

### ESP A:
- Skonfigurować WiFiManager
- Dodać dane Sinric Pro: `API_KEY`, `DEVICE_ID`
- Używa EEPROM do zapamiętywania komend

### ESP B:
- Brak konfiguracji sieci – tylko ESP-NOW
- Wake co X sekund (np. `ESP.deepSleep(X * 1e6)`)

### Biblioteki:
- `ESP8266WiFi`
- `ESPAsyncWebServer`
- `ESPAsyncTCP`
- `ESP8266EEPROM`
- `ArduinoJson`
- `WiFiManager`
- `espnow`
- `SinricPro`

## 🛠️ Przykładowe zastosowania
- Automatyczne otwieranie okna łazienkowego na podstawie komend i automatyzacji Google Home
- System pasywny do wentylacji w kuchni, piwnicy, oranżerii
- Rozwiązania off-grid sterowane bateryjnie


