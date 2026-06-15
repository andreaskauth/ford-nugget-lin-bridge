# Mitwirken am Nugget LIN-Bus Bridge Projekt

Vielen Dank für dein Interesse! Beiträge in jeder Form sind willkommen.

## Wie kann ich helfen?

### 🐛 Bugs melden

Nutze die [Issue Templates](https://github.com/USERNAME/nugget-lin-bridge/issues/new/choose) und gib so viele Informationen wie möglich:

- Firmware-Version
- Verwendete Hardware (ESP32-Variante, LIN-Converter)
- Genaues Verhalten und erwartetes Verhalten
- Serial-Log oder Web-UI Screenshots
- Bus-Scanner Ergebnisse falls relevant

### 💡 Features vorschlagen

Issue mit dem Label `enhancement` öffnen. Beschreibe:
- Was soll das Feature tun?
- Warum ist es nützlich?
- Gibt es bekannte Hindernisse?

### 🔬 Protokoll-Analyse

Besonders wertvoll sind:
- Live-Daten von noch nicht dekodierten Frames (0x20, 0x21)
- Daten von anderen Wohnmobilmodellen mit gleicher Elektronik
- Vergleich mit Original-Display-Verhalten

### 🔧 Code-Beiträge

1. Repository forken
2. Feature-Branch erstellen: `git checkout -b feature/meine-aenderung`
3. Änderungen committen mit aussagekräftiger Message
4. Pull Request öffnen gegen `main`

#### Code-Stil

- C++ für Firmware (Arduino-Style)
- Deutsche Kommentare im Code sind okay — das Projekt richtet sich primär an deutschsprachige Nutzer
- Web-UI Strings: Deutsch
- MQTT Topics und Discovery: technisches Englisch (snake_case)
- Versionsnummer in Header und Web-UI mit aktualisieren

#### Vor dem Pull Request

- [ ] Firmware kompiliert ohne Warnungen
- [ ] Auf echter Hardware getestet
- [ ] Bestehende Funktionen weiterhin OK
- [ ] CHANGELOG.md aktualisiert

## Frame-Protokoll dokumentieren

Wenn du einen Frame dekodiert hast:

1. Eintrag in [docs/protocol/frames.md](docs/protocol/frames.md) hinzufügen
2. Quelle der Information angeben (eigene Messung, Community, Datenblatt)
3. Verifikations-Status angeben (vermutet / teilweise / verifiziert)
4. Beispiel-Daten mit Erklärung

## Verhaltenskodex

Sei freundlich und respektvoll. Wir sind alle Hobbyisten die unser Hobby teilen.

## Fragen?

Öffne eine [Discussion](https://github.com/USERNAME/nugget-lin-bridge/discussions) — keine Frage ist zu klein!
