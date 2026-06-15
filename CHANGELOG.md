# Changelog

Alle wichtigen Änderungen werden in dieser Datei dokumentiert.

Format basierend auf [Keep a Changelog](https://keepachangelog.com/de/1.0.0/),
Versionierung nach [Semantic Versioning](https://semver.org/lang/de/).

## [9.9] - 2026-06-15

### Hinzugefügt
- Dometic PerfectCharge SMP439A Ladegerät vollständig integriert
  - Frame 0x18: Ladespannung
  - Frame 0x19: Ladestrom + Status
  - 230V-Status: Verbunden/Nicht verbunden mit 30s Timeout
- MQTT Sensoren: `charger_status`, `charger_strom`, `charger_spannung`
- Ladegerät-Card in Web-UI
- Raumtemperatur-Offset für Heizung (-2°C) — Heizung springt früher an
- Solar 0x20: B5-B7 Rohdaten im Log für Protokoll-Analyse

### Geändert
- MAX_LOG_ENTRIES: 500 → 200 (RAM-Einsparung)
- SERIAL_LOG_LINES: 80 → 50 (RAM-Einsparung)
- heizSoll Startwert: 0 → 22°C (verhindert -16°C beim ersten Start)
- payload Buffer: 1000 → 1100 Bytes (für neue MQTT-Felder)

### Behoben
- `charger_status` fehlte im MQTT JSON Format-String
- Ladegerät 0x18 Spannung jetzt mit Plausibilitäts-Check (9-20V)
- Scheduler-Zyklen korrigiert (5/10 Zyklen Rhythmus)

## [9.7] - 2026-05

### Hinzugefügt
- Frame 0x21 (Wasserstand alternativ) im Scheduler

### Geändert
- Pumpen Auto-AUS entfernt — Pumpe bleibt an bis manuell AUS

## [9.6] - 2026-05

### Hinzugefügt
- Touch-Bit Logik für Westfalia Wasserstand-Sensor
  - B0=0x01 dauerhaft, alle 15s kurz AUS für 350ms

## [9.2] - 2026-04

### Hinzugefügt
- WiFiManager Integration
- Captive Portal für WLAN + MQTT Konfiguration
- Reset-Button in Web-UI

## [9.1] - 2026-03

### Hinzugefügt
- Hella IBS Batteriesensor vollständig dekodiert (0x22, 0x25, 0x26)
- Votronic Solar Frame 0x20 (teilweise)
- NVS Persistenz für Fehler/Unbekannte Frames
- Statistik-Tracking (Kompressor-Starts, Heizungs-Laufzeit)

## [9.0] - 2026-03

### Hinzugefügt
- Erste stabile Version
- Eberspächer Airtronic M3 D4R Steuerung
- Dometic Kühlbox Steuerung
- Wasserpumpe und Licht steuerbar
- Web-UI mit Live-Status
- OTA Updates
- MQTT Auto-Discovery
- LIN-Bus Scanner
