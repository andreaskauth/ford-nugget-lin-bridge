# Raspberry Pi Touch-Display für Home Assistant

Optionaler Aufbau eines Touch-Displays im Fahrzeug, das die Home-Assistant-Oberfläche im Kiosk-Modus anzeigt.

## Hardware

- **Raspberry Pi 4B** oder 5
- **Raspberry Pi Touch Display 2** (oder kompatibles Display)
- DSI-Flachbandkabel (im Display-Lieferumfang)
- I2C-Verbindung für Touch-Controller
- 12V→5V Step-Down für die Stromversorgung
- microSD-Karte mind. 16 GB

## Software-Installation

### 1. Raspberry Pi OS Lite installieren

```bash
# Mit Raspberry Pi Imager:
# - Raspberry Pi OS Lite (64-bit)
# - WLAN und SSH vorkonfigurieren
# - Auf microSD flashen
```

### 2. Display konfigurieren

In `/boot/firmware/config.txt`:

```ini
# Touch Display 2
dtoverlay=vc4-kms-dsi-7inch
display_auto_detect=1

# I2C für Touch aktivieren
dtparam=i2c_arm=on

# HDMI deaktivieren (spart Strom)
hdmi_blanking=2
```

### 3. Home Assistant Supervised installieren

```bash
sudo apt update
sudo apt install \
  apparmor \
  cifs-utils \
  curl \
  dbus \
  jq \
  libglib2.0-bin \
  lsb-release \
  network-manager \
  nfs-common \
  systemd-journal-remote \
  systemd-resolved \
  udisks2 \
  wget

# Docker installieren
curl -fsSL get.docker.com | sh

# OS-Agent für Supervised
wget https://github.com/home-assistant/os-agent/releases/latest/download/os-agent_1.6.0_linux_aarch64.deb
sudo dpkg -i os-agent_1.6.0_linux_aarch64.deb

# Home Assistant Supervised
wget https://github.com/home-assistant/supervised-installer/releases/latest/download/homeassistant-supervised.deb
sudo apt install ./homeassistant-supervised.deb
```

### 4. Bluetooth aktivieren (für Shelly BLU etc.)

```bash
sudo apt install bluez
sudo systemctl enable bluetooth

# --experimental Flag setzen (für passive scanning)
sudo nano /usr/lib/systemd/system/bluetooth.service
```

In der Zeile `ExecStart` anhängen:
```
ExecStart=/usr/libexec/bluetooth/bluetoothd --experimental
```

Dann:
```bash
sudo systemctl daemon-reload
sudo systemctl restart bluetooth
```

### 5. Kiosk-Modus einrichten

Display-Manager installieren:
```bash
sudo apt install --no-install-recommends \
  xserver-xorg \
  x11-xserver-utils \
  xinit \
  openbox \
  chromium-browser \
  unclutter
```

Auto-Login aktivieren:
```bash
sudo raspi-config
# System Options → Boot / Auto Login → Console Autologin
```

`/home/pi/.bash_profile` erweitern:
```bash
if [ -z "$DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
    startx
fi
```

`/home/pi/.xinitrc` erstellen:
```bash
#!/bin/bash
xset -dpms
xset s off
xset s noblank
unclutter -idle 0.1 -root &

# Warte auf HA
while ! curl -s http://localhost:8123 > /dev/null; do
    sleep 2
done

chromium-browser \
  --noerrdialogs \
  --disable-infobars \
  --kiosk \
  --incognito \
  --disable-features=Translate \
  "http://localhost:8123/lovelace/nugget?kiosk"
```

Ausführbar machen:
```bash
chmod +x /home/pi/.xinitrc
```

### 6. Lovelace Dashboard

In Home Assistant ein dediziertes Dashboard `nugget` anlegen mit:
- Großen, touch-freundlichen Cards
- Direkter Sicht auf wichtigste Werte
- Steuerung für Heizung, Pumpe, Licht

Vorlage in [../homeassistant/lovelace_dashboard.yaml](../homeassistant/lovelace_dashboard.yaml).

## Kiosk-Mode Add-on (alternativ)

Statt URL-Parameter `?kiosk` kann auch das HACS Add-on **Kiosk Mode** (von NemesisRE) verwendet werden — Header, Sidebar und Toolbar werden dann komplett ausgeblendet.

## Bekannte Probleme

### Touch funktioniert nicht
- I2C-Adresse prüfen: `sudo i2cdetect -y 1`
- Display-Kabel beidseitig kontaktieren

### HA startet nicht beim Boot
- Service prüfen: `sudo systemctl status hassio-supervisor`
- Logs: `journalctl -u hassio-supervisor -f`

### Bildschirm flackert
- HDMI-Ausgang in config.txt deaktivieren (siehe oben)
- Stromversorgung prüfen (mind. 3A für Pi 4B mit Display)
