# Home Assistant Integration

## Voraussetzungen

- Home Assistant 2024.x oder neuer
- Mosquitto MQTT Broker (Add-on oder extern)
- ESP32 mit konfiguriertem MQTT (über Captive Portal)

## Auto-Discovery

Die Bridge sendet automatisch MQTT Discovery Nachrichten unter:
```
homeassistant/sensor/nugget_<sensor>/config
homeassistant/binary_sensor/nugget_<sensor>/config
homeassistant/switch/nugget_<sensor>/config
homeassistant/number/nugget_<sensor>/config
```

Nach dem ersten Start erscheinen alle Entities unter:
**Einstellungen → Geräte & Dienste → MQTT → Ford Nugget LIN-Bus**

## Verfügbare Entities

### Sensoren

| Entity | Beschreibung | Einheit |
|--------|--------------|---------|
| `sensor.nugget_t_innen` | Innentemperatur | °C |
| `sensor.nugget_t_aussen` | Außentemperatur | °C |
| `sensor.nugget_t_kuehlbox` | Kühlbox-Temperatur | °C |
| `sensor.nugget_t_auslass` | Heizung Auslasstemperatur | °C |
| `sensor.nugget_wasser_frisch` | Frischwasser-Stand | % |
| `sensor.nugget_wasser_grau` | Grauwasser-Stand | % |
| `sensor.nugget_wasser_hahn` | Wasserhahn Status | offen/zu |
| `sensor.nugget_zuendung` | Zündung | AN/AUS |
| `sensor.nugget_motor` | Motor | LÄUFT/AUS |
| `sensor.nugget_heiz_status` | Heizung Status | - |
| `sensor.nugget_heiz_fehler` | Heizung Fehlercode | - |
| `sensor.nugget_kuehl_komp` | Kompressor | LÄUFT/AUS |
| `sensor.nugget_kuehl_stufe` | Kühlstufe | 0-5 |
| `sensor.nugget_licht_hell` | Licht Helligkeit | 0-10 |
| `sensor.nugget_batt_volt` | Batterie Spannung | V |
| `sensor.nugget_batt_amp` | Batterie Strom | A |
| `sensor.nugget_batt_soc` | State of Charge | % |
| `sensor.nugget_batt_soh` | State of Health | % |
| `sensor.nugget_batt_temp` | Batterie Temperatur | °C |
| `sensor.nugget_solar_strom` | Solar Ladestrom | A |
| `sensor.nugget_solar_spannung` | Panel-Spannung | V |
| `sensor.nugget_solar_modus` | Solar Lademodus | - |
| `sensor.nugget_charger_status` | Ladegerät Status | - |
| `sensor.nugget_charger_strom` | Ladegerät Ladestrom | A |
| `sensor.nugget_charger_spannung` | Ladegerät Spannung | V |

### Schalter

| Entity | Beschreibung |
|--------|--------------|
| `switch.nugget_heizung` | Standheizung AN/AUS |
| `switch.nugget_pumpe` | Wasserpumpe AN/AUS |
| `switch.nugget_licht` | Licht AN/AUS |
| `switch.nugget_kuehlbox` | Kühlbox AN/AUS |

### Regler

| Entity | Beschreibung | Bereich |
|--------|--------------|---------|
| `number.nugget_heiz_soll` | Soll-Temperatur Heizung | 15-28°C |
| `number.nugget_kuehl_stufe` | Kühlstufe | 1-5 |
| `number.nugget_licht_hell` | Helligkeit | 1-10 |

## Beispiel-Dashboard

Siehe [homeassistant/lovelace_dashboard.yaml](../homeassistant/lovelace_dashboard.yaml).

## Beispiel-Automatisierungen

Siehe [homeassistant/automations/](../homeassistant/automations/).

Beispiele:
- Kühlbox Schimmelschutz (Alarm bei Feuchte >75% und Temp >10°C)
- Ladegerät Status invertieren (Binary Sensor)
- Heizung bei Unterschreitung einer Mindesttemperatur

## Ladegerät als Binary Sensor

Da die Firmware Strings `"Verbunden"` und `"Nicht verbunden"` sendet, kann ein `binary_sensor` definiert werden:

```yaml
binary_sensor:
  - platform: mqtt
    name: "Ladegerät"
    unique_id: nugget_charger_binary
    state_topic: "homeassistant/nugget/state"
    value_template: >
      {% if value_json.charger_status == 'Verbunden' %}ON{% else %}OFF{% endif %}
    payload_on: "ON"
    payload_off: "OFF"
    icon: mdi:ev-station
    device_class: plug
```

## Troubleshooting

### Entities erscheinen nicht in HA

1. Prüfen ob MQTT-Verbindung steht: Web-UI `http://nugget.local` zeigt "Bus: aktiv"
2. MQTT Explorer öffnen und Topic `homeassistant/sensor/nugget_*/config` prüfen
3. ESP32 neu starten — Discovery wird beim Connect erneut gesendet

### Werte sind veraltet

- Frame muss tatsächlich auf dem Bus aktiv sein
- Manche Frames (0x18/0x19) antworten nur bei 230V Anschluss
- Bus-Scanner in der Web-UI zeigt aktuell antwortende IDs
