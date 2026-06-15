# Frame-Definitionen Detail

## 0x01 — Kühlbox-Steuerung (Master → Slave)

**Länge:** 8 Bytes
**Sender:** Master (Display oder ESP32)
**Zyklus:** 350ms

| Byte | Bedeutung | Werte |
|------|-----------|-------|
| 0 | Pumpe + Touch-Bit | Bit 0: Touch (Wasserstand-Aktivierung), Bit 1: Pumpe AN |
| 1 | Kühl-Stufe | `(stufe & 0x07) << 4` — Stufen 0-5 |
| 2-7 | Reserviert | 0x00 |

**Touch-Bit Logik (wichtig für Wasserstand):**
- Original-Display: Bit0 dauerhaft AN, alle 15s kurz AUS für 350ms
- Aktiviert den Wasserstand-Sensor in Frame 0x02 Byte 5

## 0x02 — Sensoren (Slave → Master)

**Länge:** 8 Bytes
**Sender:** Westfalia/Hobby Sensor-Modul

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | Innentemperatur | `(120 - Byte) / 2` in °C |
| 1 | Außentemperatur | `(120 - Byte) / 2` in °C |
| 2-3 | Reserviert (0xD7 0xD7) | - |
| 4 | Kühlbox-Temperatur | `25 - Byte` in °C |
| 5 | Wasserstand | High-Nibble: Grau, Low-Nibble: Frisch (0-4 = 0-100%) |
| 6 | Reserviert | 0x00 |
| 7 | Wasserhahn | Bit 0: 1 = OFFEN |

## 0x04 — Status (Slave → Master)

**Länge:** 3 Bytes

| Byte | Bedeutung |
|------|-----------|
| 0 | Bit 0: Zündung, Bit 2: Motor läuft |
| 1 | 0x02 = Kompressor läuft, 0x00 = AUS |
| 2 | Reserviert |

## 0x05 — Licht-Steuerung (Master → Slave)

**Länge:** 8 Bytes
**Sender:** Master

| Byte | Bedeutung |
|------|-----------|
| 0 | Bit 0: Licht AN |
| 1 | High-Nibble: Farbton, Low-Nibble: Helligkeit (1-10) |
| 2-7 | Reserviert |

## 0x18 — Ladegerät Spannung (Slave → Master)

**Länge:** 4 Bytes
**Sender:** Dometic PerfectCharge SMP439A
**Antwortet nur bei 230V Anschluss**

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | Unbekannt (evtl. Max-Kapazität) | - |
| 1-2 | Ladespannung | `(Byte1 \| (Byte2 << 8)) / 1000` in V |
| 3 | Immer 0x00 | - |

Quelle: [sevenwatt.com](http://sevenwatt.com)

## 0x19 — Ladegerät Strom (Slave → Master)

**Länge:** 2 Bytes
**Sender:** Dometic PerfectCharge SMP439A

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | Ladestrom | `Byte0 / 2` in A |
| 1 | Lademodus-Status | 0x09 beobachtet (Bedeutung unklar) |

**230V-Erkennung:**
- Frame antwortet → 230V verbunden
- Timeout >30s → kein Landstrom

## 0x20 — Solar Laderegler (Slave → Master)

**Länge:** 8 Bytes
**Sender:** Votronic MPP440CI

⚠ Protokoll noch nicht vollständig verifiziert.

| Byte | Vermutung | Formel |
|------|-----------|--------|
| 0-1 | Ladestrom | `(Byte0 \| (Byte1 << 8)) / 10` in A |
| 2-3 | Panel-Spannung | `(Byte2 \| (Byte3 << 8)) / 100` in V |
| 4 | Lademodus | 0=Kein Solar, 1=Bulk, 2=Absorption, 3=Float |
| 5-7 | Unbekannt | Werden in v9.9 als Rohdaten geloggt |

**Hilfe gesucht:** Wer hat das Votronic-Protokoll dokumentiert?

## 0x21 — Wasserstand alternativ (Slave → Master)

**Länge:** 5 Bytes

🔬 In Analyse.

Beispiele:
- `01 01 0A 00 02` bei Frischwasser 25%
- `01 04 04 00 02` bei anderem Zustand

Byte 0 scheint sich mit dem Füllstand zu ändern.

## 0x22 — IBS Batterie Strom/Spannung/Temperatur (Slave → Master)

**Länge:** 7 Bytes
**Sender:** Hella IBS-1

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0-2 | Strom | `(rawI - 2000000) / 1000` in A (24-bit signed) |
| 3-4 | Spannung | `(Byte3 \| (Byte4 << 8)) / 1000` in V |
| 5 | Temperatur | `Byte5 / 2 - 40` in °C |
| 6 | Status | Reserviert |

## 0x25 — IBS SOC/SOH (Slave → Master)

**Länge:** 6 Bytes

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | State of Charge | `Byte0 / 2` in % |
| 1 | State of Health | `Byte1 / 2` in % |
| 2-5 | Reserviert | - |

## 0x26 — IBS Kapazität (Slave → Master)

**Länge:** 7 Bytes

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0-1 | Max. Kapazität | `(Byte0 \| (Byte1 << 8)) / 10` in Ah |
| 2-3 | Verfügbare Kapazität | `(Byte2 \| (Byte3 << 8)) / 10` in Ah |
| 4 | Konfigurierte Kapazität | in Ah |
| 5 | Bit 0: Kalibriert (JA/NEIN) | - |

## 0x39 — Heizung Steuerung (Master → Slave)

**Länge:** 8 Bytes
**Sender:** Master → Eberspächer Airtronic M3 D4R

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | 0xFF (immer) | - |
| 1 | Modus | 0x00=AUS, 0x02=Heizung, 0x10=Umluft |
| 2 | Soll-Temperatur | `(grad + 16) * 2` |
| 3-4 | Raumtemperatur | `(temp + 50) * 10` (int16, little endian) |
| 5-6 | Reserviert | 0x00 |
| 7 | Intensität | 0x00=COMFORT, 0x80=INTENSIV |

**Wichtig:** Die Heizung übernimmt den Raumtemperatur-Wert aus Byte 3+4 als Regelgröße. Original-Display sendet üblicherweise einen leichten Offset (-2°C) damit die Heizung früher anspringt.

## 0x3A — Heizung Status (Slave → Master)

**Länge:** 8 Bytes
**Sender:** Eberspächer Airtronic M3 D4R

| Byte | Bedeutung | Formel |
|------|-----------|--------|
| 0 | Status-Flag | 0x9A = normal |
| 1 | Fehlerklasse | 0=OK, 1=Service, 2-7=Fehler |
| 2 | Auslasstemperatur | `Byte2 - 40` in °C |
| 3 | Zwischentemperatur | `Byte3 - 40` in °C |
| 4 | Soll-Temperatur | `Byte4 / 2 - 16` in °C |
| 5 | Reserviert | - |
| 6 | Status | 0x00=AUS, 0x20=UMLUFT, 0x40=NACHLAUF, 0x50=AN |
| 7 | Reserviert | - |

**Fehlerklassen:**
- 1: Service fällig (gespeicherter Code)
- 2: Unterspannung
- 3: Überspannung
- 4: Kraftstoff/Pumpe
- 5: Überhitzung
- 6: Defekt
- 7: Temperatursensor

## 0x3B — Licht-Dimmer Status (Slave → Master)

**Länge:** 8 Bytes
Identisches Format wie 0x05, aber vom Dimmer zurückgesendet.
