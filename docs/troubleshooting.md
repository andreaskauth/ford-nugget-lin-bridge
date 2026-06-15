# Fehlerbehebung

## Bus-Kommunikation

### Bus zeigt "KEIN SIGNAL"

**Ursachen:**
1. LIN-Converter nicht angeschlossen oder defekt
2. 12V-Versorgung fehlt am FST T151
3. SLP-Pin nicht auf 3V3
4. RX/TX vertauscht

**Diagnose:**
1. Spannung am 12V-Eingang des FST T151 messen
2. Spannung an SLP gegen GND messen (muss 3.3V sein)
3. ESP32 Serial-Monitor: kommen `[POLL]` Meldungen?
4. Wenn `[POLL]` da aber keine Antwort: RX/TX am Converter tauschen

### Frames werden gesendet aber keine Antworten

**Ursache:** ESP32 ist Master-fähig, sendet aber Slaves antworten nicht.

**Lösung:**
- Original-Display muss abgeklemmt sein ODER
- Bus-Master-Modus in der Web-UI aktivieren

## Web-UI

### nugget.local nicht erreichbar

**Lösung:**
1. IP-Adresse direkt eingeben (im Router nachschauen)
2. mDNS auf dem Computer aktiv? (Windows: Bonjour, Android: nativ)
3. ESP32 im richtigen WLAN?

### UI lädt aber Werte fehlen

**Ursache:** Browser-Cache oder JavaScript-Fehler.

**Lösung:**
1. Strg+F5 (Cache umgehen)
2. Browser-Konsole (F12) auf Fehler prüfen
3. ESP32 neu starten

### UI hängt, Bedienelemente reagieren nicht

**Ursache:** Heap-Erschöpfung — passiert wenn Firmware-Patches zu viele kleine String-Allokationen einbauen.

**Lösung:**
- Auf vorherige stabile Version OTA-Update zurück
- ESP32 neu starten

## MQTT

### Sensoren erscheinen nicht in HA

**Diagnose:**
1. MQTT Explorer öffnen
2. Topic `homeassistant/sensor/nugget_*/config` prüfen
3. Topic `homeassistant/nugget/state` prüfen — kommen Werte an?

**Lösungen:**
- ESP32 neu starten (Discovery wird neu gesendet)
- MQTT-Broker erreichbar? Test mit `mosquitto_sub -h IP -t '#' -v`
- HA → Einstellungen → Geräte & Dienste → MQTT → "Konfigurieren" → "MQTT-Nachricht überwachen"

### Werte sind veraltet

- Frame wird auf dem Bus tatsächlich übertragen? Bus-Scanner in Web-UI nutzen
- Manche Frames antworten nur unter Bedingungen:
  - `0x18`/`0x19`: Nur bei 230V Anschluss
  - `0x20`: Nur bei Sonnenschein
  - `0x39`: Master-Frame, kein Slave antwortet

### Sensor zeigt "Unbekannt" oder "Nicht verbunden"

Normal wenn:
- Gerät nicht eingeschaltet
- Bus-Verbindung kurz unterbrochen
- Gerät hat noch nicht geantwortet seit Start

Timeout vor "Nicht verbunden":
- Ladegerät 0x19: 30 Sekunden

## Heizung

### Heizung springt zu spät an

**Ursache:** Raumtemperatur wird zu warm gemeldet.

**Lösung:** Offset in Firmware erhöhen (z.B. von -2°C auf -3°C):
- Datei `firmware/nugget_lin.ino` öffnen
- Stellen mit `tInnen - 2.0f` suchen
- Auf `tInnen - 3.0f` ändern
- Neu kompilieren und OTA hochladen

### Heizung schaltet ständig ab/an

**Ursache:** Hysterese zu klein oder Raumtemperatur schwankt stark.

**Lösung:**
- Temperatursensor-Position prüfen (nicht in der Nähe der Heizung)
- Sollwert minimal höher einstellen
- COMFORT statt INTENSIV Modus nutzen

### Service fällig wird angezeigt

Das ist ein gespeicherter Fehlercode der Eberspächer. Bedeutet:
- Heizung sollte zum Service
- Wartung empfohlen (Brennereinsatz reinigen, Düse prüfen)

Code löschen kann nur eine Eberspächer-Werkstatt mit Diagnose-Tool.

## Bekannte Hardware-Probleme

### ESP32 hängt sich auf

**Ursachen:**
1. Watchdog-Reset durch zu lange Funktionsaufrufe
2. Stack-Overflow bei verschachtelten Funktionen
3. Stromversorgung instabil

**Lösung:**
- Stabile 5V-Versorgung sicherstellen (mind. 500mA)
- Heap-Größe in v9.9 wurde bereits optimiert

### WLAN-Verbindung bricht ab

- 2.4 GHz nutzen, nicht 5 GHz
- Bei Mesh-Routern: Roaming kann Probleme machen
- WiFi-Signal verbessern (Antenne extern)

## Wenn nichts hilft

1. Firmware-Version notieren
2. Web-UI Serial-Log speichern
3. Bus-Scanner Ergebnis speichern
4. Issue auf GitHub öffnen mit allen Infos
