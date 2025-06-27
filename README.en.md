# ESP A ↔ ESP B Communication Summary (Window Control with Sinric Pro + ESP-NOW)
🇬🇧 This document is also available in Polish: README.md
## 🎯 Project Goal

Create a battery-powered remote window control system (e.g., for a bathroom):

* Control via Google Home (through Sinric Pro)
* ESP8266 executor (ESP B) operating in ultra-low-power mode (deep sleep)
* Minimized energy consumption of ESP B
* Separation of internet communication (WiFi + Sinric Pro) from execution logic

## 🧩 Project Modules

### 🔹 ESP A – Communication Unit

* Permanently powered
* Connected to WiFi using WiFiManager
* Handles Sinric Pro
* Stores the last command state ("open", "close", "idle") in EEPROM
* Listens for ESP-NOW packets and responds to the "WAKEUP" message from ESP B
* Sends a "COMMAND" message with the saved command (cmd) in response

### 🔹 ESP B – Executor (Battery-powered)

* Stays in deep sleep most of the time (e.g., 30s)
* On wakeup:

  * Sends a "WAKEUP" message (ESP-NOW, broadcast)
  * Waits for a "COMMAND" response with the instruction to execute
  * Based on the "cmd", opens or closes the window
  * After completing the task – goes back to sleep

## 🔄 Communication Flow

```
          Google Home
              ⬇️
        [Sinric Pro Cloud]
              ⬇️
            ESP A
   🧠 Remembers command (open/close)
              ⬇️
    📡 ESP-NOW broadcast in response
        to a message from ESP B
              ⬆️
            ESP B
   💤 Wakes up every X seconds (deep sleep)
      ⬆️ sends {"type": "WAKEUP", "id": 1}
      ⬇️ receives {"type": "COMMAND", "cmd": "open", "id": 1}
              ⬇️
     ⚙️ Opens/closes window, goes back to sleep
```

## 💬 JSON Message Structure

### 🔹 ESP B ➡️ ESP A (Command Request)

```json
{
  "type": "WAKEUP",
  "id": 1
}
```

* Sent after every ESP B wakeup
* `id` – instance identifier, can be any value (for logic, not for pairing)

### 🔹 ESP A ➡️ ESP B (Command Response)

```json
{
  "type": "COMMAND",
  "cmd": "open",    // or "close", "idle"
  "id": 1
}
```

* `cmd` comes from the last user command (via Sinric Pro / Google Home)

## ⚙️ ESP A Configuration

* Uses WiFiManager – user configures WiFi via hotspot if no data is saved
* Handles Sinric Pro Switch (API Key + Device ID to be filled in)
* Keeps WiFi always active
* Receives WAKEUP broadcast via ESP-NOW
* Responds with COMMAND via broadcast
* Does not pair by MAC – ESP B can be replaced without changing ESP A code

## ⚙️ ESP B Configuration

* Initializes ESP-NOW on each wakeup
* Sends WAKEUP broadcast
* Listens for COMMAND (short reception window, e.g., 100–200ms)
* Handles the command (`cmd`):

  * "open": activates actuator to open window
  * "close": closes the window
  * "idle": does nothing
* After executing the action – goes back to deep sleep (`ESP.deepSleep()`)
