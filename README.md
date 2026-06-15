# 🚐 Nugget LIN-Bus Bridge

ESP32-basierte Bridge zwischen dem LIN-Bus eines Ford Nugget (Westfalia/Hobby Wohnmobil-Elektronik) und Home Assistant via MQTT.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-Compatible-41BDF5.svg)](https://www.home-assistant.io/)

## Vorwort

Diese Doku ist mit Claude.AI - Verschiedene Modelle entstanden und noch nicht ausgereift, jedoch vollständig funktionsfähig. Mit Version 9.0 habe ich die Firmware-Datei des LIN-Bus-ESP von AndiM (herzlichen Dank dafür) überlassen bekommen und selbst weiterentwickelt - in der Firmware findet ihr die Historie meiner Zufügungen.
Das ganze funktioniert auch ohne Homeassistant - der ESP gibt die MQTTs raus und ihr könnt damit machen was ihr wollt - mir war es aber wichtig, das alte Display vollständig zu ersetzen. Unter Issues ein paar mir bekannte Bugs, die aktuell meinen Campingalltag jedoch nicht einschränken.

## Überblick

Diese Bridge ersetzt das originale Westfalia/Hobby Displaypanel und ermöglicht:

- **Live-Auslesen** aller Werte vom LIN-Bus (Temperaturen, Wasserstände, Batterie, Solar, Ladegerät)
- **Steuerung** von Standheizung, Kühlbox, Wasserpumpe und Licht
- **MQTT Auto-Discovery** für Home Assistant
- **Web-UI** mit Live-Status, Steuerung und Diagnose
- **OTA Updates** über WLAN
- **NVS-Logging** von Fehlern und unbekannten Frames

## Hardware

- **ESP32** DevKitC oder vergleichbar
- **FST T151** LIN-Bus Converter (TJA1020 Chip)
- 12V Spannungsversorgung aus der Aufbaubatterie

### Verkabelung

| ESP32 | FST T151 | Beschreibung |
|-------|----------|--------------|
| GPIO16 (RX2) | TX | Daten vom LIN-Bus |
| GPIO17 (TX2) | RX | Daten zum LIN-Bus |
| 3V3 | SLP | Sleep dauerhaft HIGH |
| GND | GND | Masse |
| - | 12V | Aufbaubatterie |
| - | LIN | Bus-Stecker (blau/weiß) |

Detaillierter Aufbau: [docs/hardware.md](docs/hardware.md)

## Unterstützte Geräte am LIN-Bus

| ID | Gerät | Status |
|----|-------|--------|
| 0x01/0x04 | Dometic Kühlbox | ✅ Voll |
| 0x02 | Sensoren (Temp, Wasser) | ✅ Voll |
| 0x05/0x3B | Licht (Touchscreen/Dimmer) | ✅ Voll |
| 0x18/0x19 | Dometic PerfectCharge Ladegerät | ⚠ Teilweise |
| 0x20 | Votronic Solar MPP440CI | ⚠ Teilweise |
| 0x21 | Wasserstand alternativ | 🔬 Analyse |
| 0x22/0x25/0x26 | Hella IBS Batteriesensor | ✅ Voll (für Blei) |
| 0x39/0x3A | Eberspächer Airtronic M3 D4R | ✅ Voll |

Vollständige Frame-Dokumentation: [docs/protocol/](docs/protocol/)

## Schnellstart

### 1. Firmware kompilieren und flashen

```bash
# Arduino IDE öffnen
# Board: ESP32 Dev Module
# Partition Scheme: Minimal SPIFFS (1.9MB App with OTA)
# CPU Frequency: 240MHz
# Upload Speed: 921600

# Bibliotheken installieren:
# - PubSubClient (Nick O'Leary)
# - WiFiManager (tzapu)

# firmware/nugget_lin.ino öffnen und flashen
```

### 2. WLAN konfigurieren

Nach dem ersten Start öffnet sich der Hotspot **NuggetLIN** (Passwort: `nugget12`).

1. Mit dem Smartphone verbinden
2. Captive Portal öffnet sich automatisch
3. WLAN und MQTT-Daten eintragen
4. ESP32 verbindet sich und ist unter `http://nugget.local` erreichbar

### 3. Home Assistant

Die MQTT Auto-Discovery erkennt alle Sensoren automatisch.

Optional: Vorgefertigte Dashboard-Konfiguration aus [homeassistant/](homeassistant/) übernehmen.

## Display am Raspberry Pi

Optional kann ein Raspberry Pi mit Touchscreen die Home-Assistant-Oberfläche im Kiosk-Modus anzeigen. Siehe [raspi-display/README.md](raspi-display/README.md).

## Dokumentation

- [Hardware-Aufbau](docs/hardware.md)
- [LIN-Bus Protokoll](docs/protocol/README.md)
- [Frame-Definitionen](docs/protocol/frames.md)
- [Home Assistant Integration](docs/homeassistant.md)
- [Fehlerbehebung](docs/troubleshooting.md)
- [Entwicklungsverlauf](CHANGELOG.md)

## Mitwirken

Beiträge sind willkommen! Siehe [CONTRIBUTING.md](CONTRIBUTING.md).

Besonders gesucht:
- Verifikation des Votronic Solar 0x20 Protokolls
- Vollständige Dekodierung von Frame 0x21
- Tests mit anderen Westfalia/Hobby Modellen
- Lithium-Batterie Konfiguration für IBS

## Disclaimer

> ⚠ **Achtung:** Dieses Projekt ist nicht offiziell von Westfalia, Ford, Hobby, Dometic, Eberspächer oder anderen genannten Herstellern unterstützt. Die Verwendung erfolgt auf eigene Gefahr. Eingriffe in die Fahrzeugelektrik können die Herstellergarantie beeinflussen.

## Lizenz

MIT License — siehe [LICENSE](LICENSE).

## Danksagungen

- [sevenwatt.com](http://sevenwatt.com) für die Dometic PerfectCharge Protokoll-Dokumentation
- Die Community bei [Pössl/Westfalia Forum](https://www.womoforum.de/) für Hinweise zum LIN-Bus
- AndiM und weitere aus dem Nuggetforum (https://www.nuggetforum.de/)
- Alle die mit Tests und Issues beigetragen haben
