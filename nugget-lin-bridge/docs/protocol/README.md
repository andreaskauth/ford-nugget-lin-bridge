# LIN-Bus Protokoll

Das Protokoll ist nicht offiziell dokumentiert. Diese Beschreibung basiert auf:
- Eigenen Messungen und Reverse Engineering
- Community-Beiträgen (sevenwatt.com, womoforum.de)
- Analyse der originalen Westfalia/Hobby Display-Firmware

## Allgemeine Eigenschaften

- **Baudrate:** 19200 Baud
- **Frame-Format:** Klassisch LIN 2.x
- **Master:** Display (Original) oder ESP32 (diese Bridge)
- **Polling-Zyklus:** ~350ms pro Frame
- **CRC:** Enhanced Checksum (PID-basiert)

## Frame-Struktur

```
Sync Break | Sync Byte (0x55) | PID | Data (0-8 Bytes) | Checksum
```

Die **PID** (Protected ID) wird aus der 6-Bit Frame-ID berechnet:
- Bit 0-5: Frame-ID
- Bit 6: P0 = ID0 ⊕ ID1 ⊕ ID2 ⊕ ID4
- Bit 7: P1 = ¬(ID1 ⊕ ID3 ⊕ ID4 ⊕ ID5)

## Frame-Verzeichnis

Vollständige Liste in [frames.md](frames.md).

| ID | Richtung | Gerät | Status |
|----|----------|-------|--------|
| 0x01 | Master→Slave | Kühlbox-Steuerung, Pumpe, Touch-Bit | ✅ |
| 0x02 | Slave→Master | Sensoren (Temperatur, Wasser) | ✅ |
| 0x04 | Slave→Master | Kühlbox-Status, Zündung, Motor | ✅ |
| 0x05 | Master→Slave | Licht-Steuerung | ✅ |
| 0x17 | ? | Klimaanlage (Dometic FreshJet) | ❓ |
| 0x18 | Slave→Master | Ladegerät Spannung | ⚠ |
| 0x19 | Slave→Master | Ladegerät Strom + Status | ⚠ |
| 0x20 | Slave→Master | Solar (Votronic MPP440CI) | ⚠ |
| 0x21 | Slave→Master | Wasserstand alternativ | 🔬 |
| 0x22 | Slave→Master | IBS: Strom, Spannung, Temp | ✅ |
| 0x25 | Slave→Master | IBS: SOC, SOH | ✅ |
| 0x26 | Slave→Master | IBS: Kapazität, Konfiguration | ✅ |
| 0x39 | Master→Slave | Heizung Steuerung | ✅ |
| 0x3A | Slave→Master | Heizung Status | ✅ |
| 0x3B | Slave→Master | Licht-Dimmer Status | ✅ |

Legende:
- ✅ Vollständig dekodiert und verifiziert
- ⚠ Teilweise dekodiert
- 🔬 In Analyse
- ❓ Unbekannt / nicht vorhanden

## Bekannte Geräte aus Firmware-Analyse

Die originale Westfalia/Hobby-Firmware kennt:

- **Klimaanlage:** Dometic FreshJet, FreshWell
- **Heizung:** Eberspächer Airtronic, Hydronic
- **Batterie-Sensor:** Hella IBS-1
- **Solar:** Votronic PV Controller
- **Erweiterungen:** Floor Heating, Warm Water, SlideOut, IO-Box

## Mitwirken

Wenn du Frames analysiert hast die hier noch fehlen oder unklare Bytes klären kannst — Pull Request oder Issue öffnen!
