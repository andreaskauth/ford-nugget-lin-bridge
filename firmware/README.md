# Firmware

ESP32 Firmware für die Nugget LIN-Bus Bridge.

## Aktuelle Version

**v9.9** — siehe [CHANGELOG.md](../CHANGELOG.md) für Details.

## Build-Voraussetzungen

### Arduino IDE

- **Arduino IDE** 2.x
- **ESP32 Board Package** 2.0.x oder neuer

### Bibliotheken

Über Library Manager installieren:

| Bibliothek | Autor | Version |
|------------|-------|---------|
| PubSubClient | Nick O'Leary | 2.8.x |
| WiFiManager | tzapu | 2.0.16+ |

### Board-Einstellungen

- **Board:** ESP32 Dev Module
- **Partition Scheme:** Minimal SPIFFS (1.9MB App with OTA)
- **CPU Frequency:** 240MHz (WiFi/BT)
- **Flash Frequency:** 80MHz
- **Flash Mode:** QIO
- **Flash Size:** 4MB (32Mb)
- **Upload Speed:** 921600
- **PSRAM:** Disabled

## Erster Flash (USB)

1. ESP32 per USB anschließen
2. Korrekten COM-Port auswählen
3. `nugget_lin.ino` öffnen
4. **Sketch → Hochladen** (Ctrl+U)

## OTA-Update

Nach dem ersten Flash können weitere Updates per OTA erfolgen:

1. **Sketch → Kompilierte Binärdatei exportieren**
2. Im Output-Ordner die `.bin` ohne `bootloader` im Namen nehmen
3. `http://nugget.local/update` öffnen
4. Datei hochladen — ESP32 startet automatisch neu

## Erst-Konfiguration

Beim ersten Start:

1. ESP32 öffnet Hotspot **NuggetLIN** (Passwort: `nugget12`)
2. Smartphone verbinden → Captive Portal öffnet sich automatisch
3. WLAN auswählen, Passwort eingeben
4. MQTT-Konfiguration eintragen:
   - Server: IP des MQTT-Brokers
   - Port: 1883 (Standard)
   - User/Pass: aus Mosquitto-Konfiguration

Bei Bedarf zurücksetzen: **WLAN/MQTT Reset** Button in der Web-UI auf `http://nugget.local`.

## Anpassungen

### Pin-Belegung ändern

In den `#define`-Bereichen am Anfang der `.ino`:

```cpp
#define LIN_RX_PIN  16    // ESP32 RX2 (vom FST T151 TX)
#define LIN_TX_PIN  17    // ESP32 TX2 (zum FST T151 RX)
#define LIN_INH_PIN  4    // Sleep-Pin (in HW auf 3V3 fest)
```

### Heizungs-Offset anpassen

Wenn die Heizung zu spät/früh anspringt, in den drei Stellen mit `tInnen - 2.0f` den Wert ändern:

- `-1.0f` für weniger aggressives Anspringen
- `-3.0f` für früheres Anspringen

### MQTT-Topic-Prefix ändern

```cpp
#define MQTT_PREFIX  "homeassistant"
#define MQTT_DEV     "nugget"
```

## Speicherverbrauch

Aktuelle v9.9:
- **Flash:** ~87% von 1.9MB (Minimal SPIFFS Partition)
- **RAM:** ~28% von 320KB global, ~30% Heap zur Laufzeit

Reserve für ~3-4 weitere Features.

## Debugging

### Serial Monitor

115200 Baud, Newline, Carriage Return.

Beim Start werden umfangreiche Diagnose-Infos ausgegeben.

### Web-UI Diagnose

- `http://nugget.local/serial` — Serial-Log aus dem RAM
- `http://nugget.local/data` — JSON mit aktuellen Werten
- `http://nugget.local/scan/start` — Bus-Scanner

### Häufige Probleme

**Kein Bus-Signal:**
- LIN-Converter Verkabelung prüfen (RX/TX nicht vertauscht?)
- SLP-Pin am FST T151 muss auf 3V3 liegen
- 12V-Versorgung des Converters prüfen

**OTA schlägt fehl:**
- Genug freier Flash? Partition Scheme korrekt?
- WLAN-Signal ausreichend stark?
- Beim OTA nicht den ESP32 vom Strom trennen

**WiFiManager startet immer wieder:**
- Anderes WLAN konfigurieren — manche Router (mesh) machen Probleme
- 2.4 GHz WLAN nutzen (kein 5 GHz)
