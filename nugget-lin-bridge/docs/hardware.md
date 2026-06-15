# Hardware-Aufbau

## Komponenten

### ESP32 DevKitC

Empfohlen wird ein **ESP32-WROOM-32** Modul auf einem DevKitC-Board.

Anforderungen:
- 240 MHz CPU
- 4 MB Flash (für OTA-Partitionierung)
- WiFi 802.11 b/g/n
- 2 freie UART (UART0 für USB-Debug, UART2 für LIN)

### FST T151 LIN-Bus Converter

Der FST T151 nutzt den **NXP TJA1020** LIN-Transceiver Chip.

Eigenschaften:
- Galvanische Trennung 12V ↔ 3.3V
- Master/Slave fähig
- Automatische Pegelanpassung
- Wake-up Funktion (nicht genutzt)

Alternativen:
- MCP2003B Breakout
- Eigenbau mit TJA1020 oder TLE7259

### Spannungsversorgung

- ESP32: 5V über USB oder Step-Down 12V→5V
- FST T151: 12V direkt aus der Aufbaubatterie
- Empfehlung: Gemeinsame Masse mit dem Fahrzeug

## Verkabelung

```
                    ┌─────────────────┐
                    │   ESP32         │
                    │                 │
                    │  GPIO16 (RX2) ──┼──→ TX     ┌──────────┐
                    │  GPIO17 (TX2) ──┼──→ RX     │ FST T151 │
                    │  3V3          ──┼──→ SLP    │  TJA1020 │
                    │  GND          ──┼──→ GND    │          │
                    │                 │           │  12V ────┼── Aufbaubatterie +
                    │                 │           │  GND ────┼── Aufbaubatterie -
                    └─────────────────┘           │  LIN ────┼── LIN-Bus (blau/weiß)
                                                  └──────────┘
```

## LIN-Bus Anschluss im Ford Nugget

Der LIN-Bus ist über einen **blauen/weißen Spade-Stecker** neben den Aufbaubatterien zugänglich.

**Wichtig:** Vor dem Anschluss das Original-Display abklemmen oder die Firmware im Master-Modus betreiben (Standard).

### Bus-Parameter

| Parameter | Wert |
|-----------|------|
| Baudrate | 19200 Baud |
| Spannungspegel | 12V |
| Master | ESP32 (oder Original-Display) |
| Slaves | Eberspächer, Dometic, Hella IBS, Votronic, etc. |

## Stromverbrauch

- ESP32 + FST T151: ~120-180 mA bei 12V (durchschnittlich)
- Im Standby (WiFi aktiv): ~80 mA
- Beim OTA Update: ~250 mA Peak

Bei Dauerbetrieb ca. 2-4 Ah pro Tag aus der Aufbaubatterie.

## Empfohlenes Gehäuse

- Hutschienen-Montage neben den Sicherungen
- IP54 oder besser bei Outdoor-Einbau
- Antenne für ESP32 extern verlegen wenn das Gehäuse aus Metall ist

## Sicherheit

- 12V-Zuleitung mit 1A-Sicherung absichern
- Verpolungsschutz empfohlen
- LIN-Bus Leitung nicht parallel zu Hochspannungsleitungen führen
