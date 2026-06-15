# 📤 So veröffentlichst du das Repository auf GitHub

Schritt-für-Schritt-Anleitung für Anfänger.

## Schritt 1: GitHub-Account anlegen

1. Auf [github.com](https://github.com) gehen
2. **Sign up** klicken und Account erstellen
3. E-Mail bestätigen

## Schritt 2: Neues Repository anlegen

1. Oben rechts auf das **+** klicken → **New repository**
2. Repository Name: `nugget-lin-bridge` (oder ein Name deiner Wahl)
3. Description: "ESP32 LIN-Bus Bridge for Ford Nugget / Westfalia camper to Home Assistant"
4. **Public** auswählen (für Open-Source)
5. **Add a README file** NICHT anhaken (wir haben schon eine)
6. **.gitignore** NICHT auswählen (haben wir schon)
7. **License** NICHT auswählen (haben wir schon)
8. **Create repository** klicken

## Schritt 3: Git lokal installieren

### Windows
[Git for Windows](https://git-scm.com/download/win) herunterladen und installieren.

### macOS
```bash
brew install git
```

### Linux
```bash
sudo apt install git
```

## Schritt 4: Konfigurieren

Einmal pro Computer:

```bash
git config --global user.name "Dein Name"
git config --global user.email "deine@email.de"
```

Die E-Mail sollte dieselbe sein wie bei GitHub.

## Schritt 5: Repository hochladen

Lade die ZIP des Repositories herunter (siehe unten) und entpacke sie. Dann im Terminal:

```bash
cd nugget-lin-bridge

# Git initialisieren
git init

# Alle Dateien hinzufügen
git add .

# Erster Commit
git commit -m "Initial commit: Nugget LIN-Bus Bridge v9.9"

# Mit GitHub verbinden (USERNAME durch deinen GitHub-Namen ersetzen)
git remote add origin https://github.com/USERNAME/nugget-lin-bridge.git

# Auf main branch umbenennen (GitHub-Standard)
git branch -M main

# Hochladen
git push -u origin main
```

Beim ersten Push wirst du nach deinem GitHub-Login gefragt. **Achtung:** Seit 2021 funktioniert das Passwort nicht mehr — du brauchst einen **Personal Access Token**:

1. GitHub → Settings → Developer settings → Personal access tokens → Tokens (classic)
2. **Generate new token (classic)**
3. Scope: `repo` anhaken
4. Token generieren und kopieren
5. Beim Push als "Passwort" verwenden

## Schritt 6: Repository einrichten

Auf der GitHub-Seite deines Repositories:

### About-Sektion (rechts oben)
- **Edit** klicken
- Description, Website, Topics setzen
- Topics: `esp32`, `lin-bus`, `home-assistant`, `mqtt`, `westfalia`, `ford-nugget`, `dometic`, `eberspacher`, `wohnmobil`, `camper`

### Issues aktivieren
- **Settings → General → Features**
- ✅ Issues
- ✅ Discussions (für Community-Fragen)

### Default Branch
- Sollte automatisch `main` sein

## Schritt 7: README polieren

Wenn das Repository online ist:

1. README.md öffnen
2. Pfade prüfen — alle relativen Links funktionieren?
3. Screenshots hinzufügen (in `docs/images/` ablegen und im README einbinden)
4. Badges anpassen (USERNAME im URL ersetzen)

## Updates später hochladen

Bei jeder Änderung:

```bash
git add .
git commit -m "Kurze Beschreibung der Änderung"
git push
```

## Wie andere mitarbeiten

Andere Nutzer können:

### 1. Issues öffnen
- Bug-Reports
- Feature-Requests
- Fragen

### 2. Discussions starten
Für offene Fragen und Erfahrungsaustausch.

### 3. Pull Requests einreichen
- Repository **Forken** (eigene Kopie machen)
- Änderungen im Fork
- **Pull Request** zurück zum Original-Repo

Du als Maintainer entscheidest dann ob du den PR übernimmst.

## Versionen taggen

Wenn eine stabile Version fertig ist:

```bash
git tag v9.9 -m "Release v9.9: Ladegerät-Integration"
git push origin v9.9
```

Dann auf GitHub: **Releases → Draft a new release** → Tag auswählen → Release-Notes schreiben.

## Community aufbauen

- Im [Pössl/Westfalia Forum](https://www.womoforum.de/) das Projekt vorstellen
- Auf Reddit r/CamperVans oder r/homeassistant teilen
- In Telegram/Discord-Gruppen erwähnen
- README übersetzen (EN, optional)

## Sicherheit

❌ **Niemals committen:**
- WLAN-Passwörter
- MQTT-Credentials  
- API-Keys
- Persönliche Daten

Wenn versehentlich passiert: Sofort die Credentials ändern und [BFG Repo-Cleaner](https://rtyley.github.io/bfg-repo-cleaner/) verwenden.

## Fragen?

GitHub hat eine sehr gute [Dokumentation](https://docs.github.com/de). Speziell:
- [Hello World Tutorial](https://docs.github.com/de/get-started/quickstart/hello-world)
- [Git Cheatsheet](https://education.github.com/git-cheat-sheet-education.pdf)

Bei Fragen einfach hier weiterfragen — ich helfe dir durch.
