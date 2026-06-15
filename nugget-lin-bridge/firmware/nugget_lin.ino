/*
 * Ford Nugget LIN-Bus Sniffer - v9.9
 *
 * ANGEPASST FÜR:
 *   - WomoLIN LIN-Interface 2.1 (TJA1021T Transceiver)
 *     → Kein INH-Pin (intern fest auf HIGH verdrahtet)
 *     → TX → GPIO17, RX → GPIO16, VCC → 3V3, GND → GND
 *     → VBat → 12V Aufbaubatterie, LIN → blauer Spade-Stecker
 *   - ESP32-DevKitC (Espressif Original, 38-Pin)
 *   - Ford Nugget Plus 2023+
 *   - Display entfernt → Master-Modus immer aktiv
 *
 * NEU in v9.9 (gegenüber v9.7):
 *   - FIX: charger_status/strom/spannung im MQTT JSON (fehlten im Format-String)
 *   - FIX: Solar 0x20 Rohdaten werden jetzt geloggt statt hasUnknown
 *   - FIX: Ladegerät 0x18 Byte0 = unbekannt, Byte1+2 = Spannung (verifiziert)
 *   - Heizung: INTENSIV als Standard statt COMFORT (näher am Original-Display)
 *   - RAM: MAX_LOG_ENTRIES 500->200, SERIAL_LOG_LINES 80->50
 *
 * v9.7 (vorherige Basis):
 *   - Dometic PerfectCharge SMP439A Ladegerät eingebaut
 *     Frame 0x18: Ladespannung (x/1000 V)
 *     Frame 0x19: Ladestrom (x/2 A), Status
 *     230V Status: antwortet = Verbunden, keine Antwort = Nicht verbunden
 *     MQTT: charger_status, charger_strom, charger_spannung
 *   - heizSoll Startwert 22°C (verhindert -16°C beim ersten Start)
 *
 * v9.6 (vorherige Basis):
 *   - Touch-Bit Fix: linSendDisplay() setzt B0=0x01 dauerhaft
 *     alle 15s kurz AUS fuer 350ms — aktiviert Wasserstand-Sensor
 *     Bestaetigt durch Original-Display CSV-Analyse (v11.2)
 *   - Wasserstand: Byte 5 von Frame 0x02 (High-Nibble=Grau, Low-Nibble=Frisch)
 *   - Licht-Fix: B0 |= 0x80 bei User-Befehl (Original-Display Verhalten)
 *   - Frame 0x21 in Scheduler eingebaut (alle 5 Zyklen)
 *
 * v9.5 (vorherige Basis):
 *   - LIN-Bus Scanner: scannt alle IDs 0x01-0x3F
 *     Web-UI Button "LIN Scanner" -> Ergebnisse live
 *
 * v9.4 (vorherige Basis):
 *   - Pumpen Auto-AUS Timeout entfernt — Steuerung komplett über MQTT/Web
 *
 * v9.3 (vorherige Basis):
 *   - INH/SLP-Pin auf GPIO4 wieder aktiviert (für FST T151 / TJA1020)
 *     GPIO4 wird beim Start auf HIGH gesetzt → Transceiver aktiv
 *     WomoLIN-Nutzer: GPIO4 einfach offen lassen (schadet nicht)
 *
 * v9.2 (vorherige Basis):
 *   - WiFiManager Integration: Kein hardcodiertes WLAN/MQTT mehr!
 *   - Beim ersten Start (oder nach Reset): ESP32 öffnet Hotspot "NuggetLIN"
 *     Passwort: nugget12 → Browser öffnet Konfigurationsseite automatisch
 *   - WLAN-SSID, Passwort, MQTT-Broker, User, Passwort über Captive Portal
 *     konfigurierbar → wird im NVS-Flash gespeichert
 *   - Web-UI: neuer Button "WLAN/MQTT zurücksetzen" → öffnet Hotspot neu
 *   - Fallback: Kein WLAN nach 60s → Hotspot öffnet automatisch
 *   - Hotspot-IP: 192.168.4.1 (alternativ: http://nugget.local im Hotspot)
 *
 * Bibliotheken: PubSubClient + WiFiManager (tzapu) — in Arduino IDE installieren!
 *
 * NEU in v9.1 (gegenüber v9.0):
 *   - KEIN LIN_INH_PIN mehr (WomoLIN 2.1 benötigt keinen Enable-Pin)
 *   - MQTT Auto-Discovery für IBS Batterie-Sensoren wieder aktiviert
 *   - Votronic Solar Frame 0x20 hinzugefügt
 *
 * Hardware:  ESP32-DevKitC + WomoLIN LIN-Interface 2.1
 * GPIO:      RX=16, TX=17 (kein INH!)
 * LIN-Bus:   Weißer/blauer Spade-Stecker neben Aufbaubatterie, 19200 Baud
 * Web-UI:    http://nugget.local
 * MQTT:      Mosquitto Add-on in Home Assistant
 *
 * Geräte am Bus:
 *   - Eberspächer Airtronic M3 D4R (Standheizung, 4kW Diesel)
 *   - Dometic Toploader Kühlbox (Kompressor, Stufen 0–5)
 *   - Hella IBS Sensor (Batterie-Management, 200Ah)
 *   - Votronic Solar-Laderegler
 *   - LED-Innenraumbeleuchtung
 *   - Wasserpumpe
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <driver/uart.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <PubSubClient.h>   // Bibliothek: PubSubClient by Nick O'Leary
#include <WiFiManager.h>    // Bibliothek: WiFiManager by tzapu

// ============================================================
// HOTSPOT-FALLBACK — wird geöffnet wenn kein WLAN bekannt
// ============================================================
#define WIFIMGR_SSID      "NuggetLIN"   // Hotspot-Name
#define WIFIMGR_PASS      "nugget12"    // Hotspot-Passwort (min. 8 Zeichen)
#define WIFIMGR_TIMEOUT   60            // Sekunden bis Hotspot aufgeht (0 = sofort)

// ============================================================
// MQTT-KONFIGURATION (wird über Captive Portal gesetzt & im Flash gespeichert)
// ============================================================
// Diese Werte werden zur Laufzeit aus dem NVS geladen — nicht hier ändern!
char   cfg_mqtt_broker[40] = "192.168.1.100";  // Default-Vorschlag im Portal
char   cfg_mqtt_port[6]    = "1883";
char   cfg_mqtt_user[32]   = "";
char   cfg_mqtt_pass[32]   = "";

const char* MQTT_CLIENT   = "nugget-lin-sniffer";
const char* MQTT_PREFIX   = "homeassistant";   // HA Discovery Prefix
const char* MQTT_DEV      = "nugget";          // Device-ID

// NVS-Namespace für MQTT-Konfiguration
Preferences prefCfg;

// MQTT-Laufzeit-Objekte (werden nach WLAN-Verbindung initialisiert)
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// WiFiManager globale Instanz
WiFiManager wifiManager;
bool        wifiConfigMode = false;  // true = Hotspot aktiv

// ============================================================
// GPIO-PINS — WomoLIN Interface 2.1
// FST T151 / TJA1020:
//   TX  → ESP32 GPIO16 (RX2)
//   RX  → ESP32 GPIO17 (TX2)
//   SLP → ESP32 GPIO4  (INH — HIGH = aktiv)
//   GND → ESP32 GND + Fahrzeugmasse
//   12V → 12V Aufbaubatterie
//   LIN → LIN-Bus Stecker
// WomoLIN 2.1: INH intern verdrahtet → GPIO4 offen lassen
// ============================================================
#define LIN_RX_PIN   16
#define LIN_TX_PIN   17
#define LIN_INH_PIN   4   // SLP/INH — HIGH = Transceiver aktiv
#define LIN_BAUD     19200


// ============================================================
// LIN-BUS SCANNER
// ============================================================
#define SCAN_ID_MIN  0x01
#define SCAN_ID_MAX  0x3F
#define SCAN_SLOT_MS 80UL

struct ScanResult {
  uint8_t  id;
  bool     responded;
  uint8_t  data[10];
  uint8_t  dataLen;
  uint8_t  crc;
  bool     crcOk;
};

ScanResult   scanResults[SCAN_ID_MAX + 1];
uint8_t      scanCurrentId = 0;
bool         scanActive    = false;
bool         scanDone      = false;
unsigned long scanSlotTime = 0;

// RAM-Puffer für Live-Log und CSV-Download
#define MAX_LOG_ENTRIES  200
#define MAX_FRAME_BYTES   10

// NVS-Ringpuffer-Größen
#define NVS_ERROR_MAX    50
#define NVS_UNKNOWN_MAX 100

// Kühlbox Temperatur Korrektur
#define KUEHLBOX_TEMP_OFFSET  0.0f

// ============================================================
// NVS NAMESPACES & SCHLÜSSEL
// ============================================================
Preferences prefErr;
Preferences prefUnk;
Preferences prefStat;

int nvsErrHead  = 0;
int nvsErrCount = 0;
int nvsUnkCount = 0;

struct NvsStats {
  uint32_t kompressorStarts;
  uint32_t heizungLaufzeitSek;
  float    ibsMinVolt;
  float    ibsMaxVolt;
  float    ibsMinTemp;
  float    ibsMaxTemp;
  float    ibsMinSoc;
} nvsStats;

bool    lastKompressorState = false;
uint8_t lastHeizStatus      = 0x00;
uint8_t lastHeizIntensiv    = 0x00;
unsigned long heizStartMs   = 0;

// ============================================================
// FRAME-DEFINITIONEN
// ============================================================
struct FrameDef {
  uint8_t     id;
  uint8_t     dataLen;
  const char* name;
  const char* desc;
};

const FrameDef FRAME_DEFS[] = {
  { 0x01,   8, "DISPLAY_01",   "Touchscreen → Geräte (Kühlung, Pumpe)" },
  { 0x02,   8, "SENSORS_02",   "Zentrale Elektronik (Temp, Wasser)" },
  { 0x04,   3, "STATUS_04",    "Kühlbox + Fahrzeugsignale (3-Byte Frame, Enhanced CRC)" },
  { 0x05,   8, "LED_05",       "LED Helligkeit/Ton (Touchscreen)" },
  { 0x06,   0, "UNKNOWN_06",   "Kein Response" },
  { 0x0C,   0, "UNKNOWN_0C",   "Kein Response" },
  { 0x17,   0, "UNKNOWN_17",   "Kein Response" },
  { 0x1B,   8, "UNKNOWN_1B",   "Unbekannt" },
  // NEU v9.1: 0x20 Votronic Solar — war "Kein Response", jetzt 8 Bytes
  { 0x18,   4, "CHARGER_18",   "Dometic PerfectCharge SMP439A (Spannung)" },
  { 0x19,   2, "CHARGER_19",   "Dometic PerfectCharge SMP439A (Strom/Status)" },
  { 0x20,   8, "SOLAR_20",     "Votronic Solar-Laderegler" },
  { 0x21,   5, "WATER_21",     "Westfalia Wasserstand (alternativ)" },
  { 0x22,   8, "IBS_CURR_22",  "Hella IBS Strom/Spannung/Temp" },
  { 0x25,   7, "IBS_SOC_25",   "Hella IBS SOC/SOH" },
  { 0x26,   7, "IBS_CAP_26",   "Hella IBS Kapazität" },
  { 0x38,   0, "ARTEFAKT_38",  "Parser-Artefakt (kein echter Frame)" },
  { 0x39,   8, "HEATER_CMD",   "Eberspächer Heizung Befehl" },
  { 0x3A,   8, "HEATER_STAT",  "Eberspächer Heizung Status" },
  { 0x3B,   8, "LED_3B",       "LED Helligkeit/Ton (Dimmer)" },
  { 0x3C,   8, "DANHAG_3C",    "Danhag intern: Diagnose/Status" },
  { 0x3D,   8, "DANHAG_3D",    "Danhag intern: Echtzeit-Daten" },
};
const uint8_t FRAME_DEF_COUNT = sizeof(FRAME_DEFS) / sizeof(FrameDef);

#define IBS_LOG_INTERVAL_MS  5000
bool isIBSFrame(uint8_t id) {
  return (id == 0x22 || id == 0x25 || id == 0x26);
}

struct ParserStats {
  uint32_t rxTimeout;
  uint32_t rxLengthOverflow;
  uint32_t rxParityError;
} parserStats;
unsigned long lastParserStatPrint = 0;

// ============================================================
// SERIAL-LOG RINGPUFFER
// ============================================================
#define SERIAL_LOG_LINES  50
#define SERIAL_LOG_WIDTH 120
char     serialLogBuf[SERIAL_LOG_LINES][SERIAL_LOG_WIDTH];
uint8_t  serialLogHead  = 0;
uint8_t  serialLogCount = 0;
portMUX_TYPE serialLogMux = portMUX_INITIALIZER_UNLOCKED;

void serialLog(const char* line) {
  portENTER_CRITICAL(&serialLogMux);
  strncpy(serialLogBuf[serialLogHead], line, SERIAL_LOG_WIDTH - 1);
  serialLogBuf[serialLogHead][SERIAL_LOG_WIDTH - 1] = '\0';
  serialLogHead = (serialLogHead + 1) % SERIAL_LOG_LINES;
  if (serialLogCount < SERIAL_LOG_LINES) serialLogCount++;
  portEXIT_CRITICAL(&serialLogMux);
}

void sLog(const char* fmt, ...) {
  char buf[SERIAL_LOG_WIDTH];
  va_list args; va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
  Serial.print(buf); serialLog(buf);
}
void sLogLn(const char* fmt, ...) {
  char buf[SERIAL_LOG_WIDTH];
  va_list args; va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
  Serial.println(buf);
  size_t l = strlen(buf);
  if (l < SERIAL_LOG_WIDTH - 1) { buf[l] = '\n'; buf[l+1] = '\0'; }
  serialLog(buf);
}

// ============================================================
// RAM LOG-STRUKTUR
// ============================================================
struct LogEntry {
  unsigned long timestamp;
  unsigned long deltaMs;
  uint8_t  id;
  uint8_t  data[MAX_FRAME_BYTES];
  uint8_t  dataLen;
  uint8_t  crc;
  bool     crcOk;
  bool     changed;
  bool     hasError;
  bool     hasUnknown;
  String   decoded;
};

LogEntry logEntries[MAX_LOG_ENTRIES];
int      logHead  = 0;
int      logCount = 0;
portMUX_TYPE logMux = portMUX_INITIALIZER_UNLOCKED;

struct FrameState {
  uint8_t  data[MAX_FRAME_BYTES];
  uint8_t  dataLen;
  bool     valid;
  unsigned long lastSeen;
  unsigned long lastLogged;
  uint32_t count;
};
FrameState frameStates[64];

// Ladegerät: Zeit der letzten Antwort von 0x19
// Wenn > CHARGER_TIMEOUT_MS keine Antwort → Nicht verbunden
#define CHARGER_TIMEOUT_MS 30000UL
unsigned long lastChargerSeen = 0;

// ============================================================
// NVS-HILFSFUNKTIONEN
// ============================================================
uint32_t calcFrameHash(uint8_t id, uint8_t* d, uint8_t len) {
  uint32_t h = (uint32_t)id;
  for (uint8_t i = 0; i < len; i++) h = h * 31 + d[i];
  return h;
}

bool nvsUnkHashExists(uint32_t hash) {
  char key[8];
  for (int i = 0; i < nvsUnkCount; i++) {
    snprintf(key, sizeof(key), "h%02d", i);
    if (prefUnk.getUInt(key, 0xFFFFFFFF) == hash) return true;
  }
  return false;
}

String serializeEntry(unsigned long ts, uint8_t id, uint8_t* d, uint8_t len, const String& dec) {
  String s = String(ts) + ":" + String(id, HEX) + ":";
  for (uint8_t i = 0; i < len; i++) {
    char hex[3]; snprintf(hex, sizeof(hex), "%02X", d[i]);
    s += hex;
  }
  s += ":" + dec;
  if (s.length() > 200) s = s.substring(0, 200);
  return s;
}

void nvsWriteError(unsigned long ts, uint8_t id, uint8_t* d, uint8_t len, const String& dec) {
  char key[6]; snprintf(key, sizeof(key), "e%02d", nvsErrHead);
  String val = serializeEntry(ts, id, d, len, dec);
  prefErr.putString(key, val);
  nvsErrHead = (nvsErrHead + 1) % NVS_ERROR_MAX;
  if (nvsErrCount < NVS_ERROR_MAX) nvsErrCount++;
  prefErr.putInt("head", nvsErrHead);
  prefErr.putInt("cnt",  nvsErrCount);
}

void nvsWriteUnknown(unsigned long ts, uint8_t id, uint8_t* d, uint8_t len, const String& dec) {
  if (nvsUnkCount >= NVS_UNKNOWN_MAX) return;
  uint32_t hash = calcFrameHash(id, d, len);
  if (!nvsUnkHashExists(hash)) {
    char kVal[8]; snprintf(kVal, sizeof(kVal), "u%02d", nvsUnkCount);
    char kHash[8]; snprintf(kHash, sizeof(kHash), "h%02d", nvsUnkCount);
    String val = serializeEntry(ts, id, d, len, dec);
    prefUnk.putString(kVal, val);
    prefUnk.putUInt(kHash, hash);
    nvsUnkCount++;
    prefUnk.putInt("cnt", nvsUnkCount);
  }
}

void nvsLoadIndices() {
  prefErr.begin("nugget_err", false);
  nvsErrHead  = prefErr.getInt("head", 0);
  nvsErrCount = prefErr.getInt("cnt",  0);

  prefUnk.begin("nugget_unk", false);
  nvsUnkCount = prefUnk.getInt("cnt", 0);

  prefStat.begin("nugget_stat", false);
  nvsStats.kompressorStarts    = prefStat.getUInt("kompStarts", 0);
  nvsStats.heizungLaufzeitSek  = prefStat.getUInt("heizSek",   0);
  nvsStats.ibsMinVolt          = prefStat.getFloat("ibsMinV", 99.0f);
  nvsStats.ibsMaxVolt          = prefStat.getFloat("ibsMaxV",  0.0f);
  nvsStats.ibsMinTemp          = prefStat.getFloat("ibsMinT", 99.0f);
  nvsStats.ibsMaxTemp          = prefStat.getFloat("ibsMaxT",-99.0f);
  nvsStats.ibsMinSoc           = prefStat.getFloat("ibsMinSOC", 100.0f);
}

void nvsSaveStats() {
  prefStat.putUInt("kompStarts", nvsStats.kompressorStarts);
  prefStat.putUInt("heizSek",   nvsStats.heizungLaufzeitSek);
  prefStat.putFloat("ibsMinV",  nvsStats.ibsMinVolt);
  prefStat.putFloat("ibsMaxV",  nvsStats.ibsMaxVolt);
  prefStat.putFloat("ibsMinT",  nvsStats.ibsMinTemp);
  prefStat.putFloat("ibsMaxT",  nvsStats.ibsMaxTemp);
  prefStat.putFloat("ibsMinSOC",nvsStats.ibsMinSoc);
}

void nvsClearAll() {
  prefErr.end();  prefErr.begin("nugget_err",  false); prefErr.clear();
  prefUnk.end();  prefUnk.begin("nugget_unk",  false); prefUnk.clear();
  prefStat.end(); prefStat.begin("nugget_stat",false); prefStat.clear();
  nvsErrHead  = 0; nvsErrCount = 0; nvsUnkCount = 0;
  memset(&nvsStats, 0, sizeof(nvsStats));
  nvsStats.ibsMinVolt = 99.0f; nvsStats.ibsMinTemp = 99.0f; nvsStats.ibsMinSoc = 100.0f;
  Serial.println("[NVS] Alle Daten gelöscht.");
}

// ============================================================
// STATISTIK-UPDATE
// ============================================================
void updateStats(uint8_t id, uint8_t* d, uint8_t len) {
  bool changed = false;

  if (id == 0x04 && len >= 2) {
    bool komp = (d[1] == 0x02);
    if (komp && !lastKompressorState) {
      nvsStats.kompressorStarts++;
      changed = true;
    }
    lastKompressorState = komp;
  }

  if (id == 0x3A && len >= 7) {
    bool heizt = (d[6] == 0x50);
    if (heizt && lastHeizStatus != 0x50) heizStartMs = millis();
    if (!heizt && lastHeizStatus == 0x50) { heizStartMs = 0; changed = true; }
    lastHeizStatus = d[6];
  }

  if (id == 0x22 && len >= 6) {
    uint16_t rawV = d[3] | ((uint16_t)d[4]<<8);
    float volt = rawV / 1000.0f;
    float temp = d[5] / 2.0f - 40.0f;
    if (volt > 5.0f && volt < 20.0f) {
      if (volt < nvsStats.ibsMinVolt) { nvsStats.ibsMinVolt = volt; changed = true; }
      if (volt > nvsStats.ibsMaxVolt) { nvsStats.ibsMaxVolt = volt; changed = true; }
    }
    if (temp > -40.0f && temp < 80.0f) {
      if (temp < nvsStats.ibsMinTemp) { nvsStats.ibsMinTemp = temp; changed = true; }
      if (temp > nvsStats.ibsMaxTemp) { nvsStats.ibsMaxTemp = temp; changed = true; }
    }
  }

  if (id == 0x25 && len >= 1) {
    float soc = d[0] / 2.0f;
    if (soc < nvsStats.ibsMinSoc && soc >= 0.0f) { nvsStats.ibsMinSoc = soc; changed = true; }
  }

  if (changed) nvsSaveStats();
}

// ============================================================
// FRAME DEKODIERUNG
// ============================================================
String decodeFrame(uint8_t id, uint8_t* d, uint8_t len, bool& hasError, bool& hasUnknown) {
  hasError   = false;
  hasUnknown = false;
  char buf[300];
  String result = "";

  switch (id) {

    case 0x01:
      if (len >= 2) {
        uint8_t coolLevel = (d[1] >> 4) & 0x07;
        bool    pump      = (d[0] >> 1) & 0x01;
        bool    touch     = (d[0]) & 0x01;
        char stufeBuf[16];
        if (coolLevel > 0) snprintf(stufeBuf, sizeof(stufeBuf), "Stufe %d", coolLevel);
        else                strcpy(stufeBuf, "AUS");
        snprintf(buf, sizeof(buf),
          "Kühlung: %s | Pumpe: %s | Touch: %s",
          stufeBuf, pump ? "AN" : "AUS", touch ? "aktiv" : "inaktiv");
        result = buf;
      }
      break;

    case 0x02:
      if (len >= 6) {
        float tInnen  = (120.0f - d[0]) / 2.0f;
        float tAussen = (120.0f - d[1]) / 2.0f;
        float tKuehl  = 25.0f - (float)d[4] + KUEHLBOX_TEMP_OFFSET;
        uint8_t wGrau   = (d[5] >> 4) & 0x0F;
        uint8_t wFrisch = d[5] & 0x0F;
        bool    hahn    = (len >= 8) ? (d[7] & 0x01) : false;
        if (tAussen < -40 || tAussen > 60) hasError = true;
        if (tInnen  < -40 || tInnen  > 60) hasError = true;
        if (tKuehl  < -25 || tKuehl  > 40) hasError = true;
        snprintf(buf, sizeof(buf),
          "T-Außen: %.1f°C | T-Innen: %.1f°C | T-Kühlbox: %.1f°C | Frisch: %d/4 | Grau: %d/4 | Hahn: %s",
          tAussen, tInnen, tKuehl, wFrisch, wGrau, hahn ? "OFFEN" : "ZU");
        result = buf;
      }
      break;

    case 0x04:
      if (len >= 2) {
        bool kompressor = (d[1] == 0x02);
        bool kontakt    = (d[0] & 0x01);
        bool motor      = (d[0] & 0x04);
        const char* b3info = "";
        if (len >= 4) {
          if      (d[3] == 0x3B) b3info = " | B3:Normal";
          else if (d[3] == 0x36) b3info = " | B3:MotorL";
          else { hasUnknown = true; b3info = " | B3:???"; }
        }
        snprintf(buf, sizeof(buf),
          "Kompressor: %s | Zündung: %s | Motor: %s%s",
          kompressor ? "AN" : "AUS", kontakt ? "AN" : "AUS", motor ? "LÄUFT" : "AUS", b3info);
        if (len >= 8) hasUnknown = true;
        result = buf;
      }
      break;

    case 0x05:
    case 0x3B:
      if (len >= 2) {
        bool    an   = d[0] & 0x01;
        bool    ctrl = (d[0] >> 7) & 0x01;
        uint8_t ton  = (d[1] >> 4) & 0x0F;
        uint8_t hell = d[1] & 0x0F;
        snprintf(buf, sizeof(buf),
          "Licht: %s | Helligkeit: %d/10 | Farbton: %d/10%s",
          an ? "AN" : "AUS", hell, ton,
          ctrl ? (id==0x05 ? " | Touch bedient" : " | Dimmer bedient") : "");
        result = buf;
      }
      break;

    // --------------------------------------------------------
    // NEU v9.1: 0x20 — Votronic Solar-Laderegler
    // Dekodierung basiert auf Votronic LIN-Protokoll Dokumentation.
    // HINWEIS: Byte-Layout noch nicht vollständig verifiziert —
    // Werte im Unbekannt-Log bis zur Bestätigung durch eigene Messung.
    // B0-B1: Solar-Ladestrom (uint16, Einheit 0.1A)
    // B2-B3: Panelspannung (uint16, Einheit 0.01V)
    // B4:    Lademodus (0x00=Kein Solar, 0x01=Bulk, 0x02=Absorption, 0x03=Float)
    // B5-B7: unbekannt → hasUnknown bis verifiziert
    // --------------------------------------------------------
    case 0x18:
      if (len >= 3) {
        // B0: unbekannt (evtl. Max-Kapazitaet Schaetzung)
        // B1+B2: Ladespannung (x/1000 V) — verifiziert via sevenwatt.com
        float spannung = ((uint16_t)d[1] | ((uint16_t)d[2] << 8)) / 1000.0f;
        if (spannung > 9.0f && spannung < 20.0f) {
          snprintf(buf, sizeof(buf), "Ladespannung: %.3fV | B0=0x%02X", spannung, d[0]);
        } else {
          snprintf(buf, sizeof(buf), "Ladespannung ungueltig: %.3fV", spannung);
          hasError = true;
        }
        result = buf;
      } else { hasUnknown = true; result = "Ladegeraet (zu wenig Bytes)"; }
      break;

    case 0x19:
      if (len >= 2) {
        lastChargerSeen = millis();
        float strom = d[0] / 2.0f;
        uint8_t status = d[1];
        const char* statusStr = "Unbekannt";
        // Status-Byte 0x09 noch nicht vollstaendig entschluesselt
        // Antwortet nur bei 230V Verbindung
        snprintf(buf, sizeof(buf), "Ladestrom: %.1fA | Status: 0x%02X", strom, status);
        result = buf;
      } else { hasUnknown = true; result = "Ladegeraet (zu wenig Bytes)"; }
      break;

    case 0x20:
      if (len >= 5) {
        float solarStrom   = ((uint16_t)d[0] | ((uint16_t)d[1]<<8)) / 10.0f;
        float panelSpannung = ((uint16_t)d[2] | ((uint16_t)d[3]<<8)) / 100.0f;
        const char* ladeModusStr = "Unbekannt";
        switch (d[4]) {
          case 0x00: ladeModusStr = "Kein Solar";   break;
          case 0x01: ladeModusStr = "Bulk";         break;
          case 0x02: ladeModusStr = "Absorption";   break;
          case 0x03: ladeModusStr = "Float";        break;
          default:   ladeModusStr = "Unbekannt"; hasUnknown = true; break;
        }
        if (solarStrom < 0 || solarStrom > 40)     hasError = true;
        if (panelSpannung < 0 || panelSpannung > 50) hasError = true;
        snprintf(buf, sizeof(buf),
          "Solar: %.1fA | Panel: %.2fV | Modus: %s",
          solarStrom, panelSpannung, ladeModusStr);
        // B5-B7: Rohdaten anzeigen fuer Protokoll-Analyse
        if (len >= 6) {
          char extra[40];
          snprintf(extra, sizeof(extra), " | B5=%02X B6=%02X B7=%02X",
            len>5?d[5]:0, len>6?d[6]:0, len>7?d[7]:0);
          strncat(buf, extra, sizeof(buf)-strlen(buf)-1);
        }
        result = buf;
      } else {
        hasUnknown = true;
        result = "Votronic Solar (zu wenig Bytes)";
      }
      break;

    case 0x22:
      if (len >= 6) {
        int32_t rawI = (int32_t)d[0] | ((int32_t)d[1]<<8) | ((int32_t)d[2]<<16);
        float   amp  = (rawI - 2000000.0f) / 1000.0f;
        float   volt = (d[3] | ((uint16_t)d[4]<<8)) / 1000.0f;
        float   temp = d[5] / 2.0f - 40.0f;
        if (volt < 9.0 || volt > 16.0) hasError = true;
        if (temp < -40 || temp > 80)   hasError = true;
        snprintf(buf, sizeof(buf),
          "Strom: %.2fA | Spannung: %.3fV | Temp: %.1f°C", amp, volt, temp);
        result = buf;
      }
      break;

    case 0x25:
      if (len >= 2) {
        float soc = d[0] / 2.0f;
        float soh = d[1] / 2.0f;
        if (soc > 100 || soh > 100) hasError = true;
        snprintf(buf, sizeof(buf), "SOC: %.1f%% | SOH: %.1f%%", soc, soh);
        result = buf;
      }
      break;

    case 0x26:
      if (len >= 4) {
        float maxCap = (d[0] | ((uint16_t)d[1]<<8)) / 10.0f;
        float avaCap = (d[2] | ((uint16_t)d[3]<<8)) / 10.0f;
        if (len >= 6) {
          uint8_t cfgCap  = d[4];
          bool    calDone = d[5] & 0x01;
          snprintf(buf, sizeof(buf),
            "Max: %.1fAh | Verfügbar: %.1fAh | Konfiguriert: %dAh | Kalibriert: %s",
            maxCap, avaCap, cfgCap, calDone ? "JA" : "NEIN");
        } else if (len >= 5) {
          uint8_t cfgCap = d[4];
          snprintf(buf, sizeof(buf),
            "Max: %.1fAh | Verfügbar: %.1fAh | Konfiguriert: %dAh", maxCap, avaCap, cfgCap);
        } else {
          snprintf(buf, sizeof(buf), "Max: %.1fAh | Verfügbar: %.1fAh", maxCap, avaCap);
        }
        result = buf;
      }
      break;

    case 0x39:
      if (len >= 3) {
        const char* mode = "UNBEKANNT";
        if (d[1] == 0x00) mode = "AUS";
        if (d[1] == 0x02) mode = "HEIZUNG AN";
        if (d[1] == 0x08) mode = "UMLUFT";
        float sollTemp = d[2] / 2.0f - 16.0f;
        const char* intensiv = (len >= 8 && d[7] == 0x80) ? "INTENSIV" : "COMFORT";
        if (d[1] == 0x08) intensiv = "UMLUFT";
        if (len >= 8) lastHeizIntensiv = d[7];
        float raumTemp = -99;
        if (len >= 5) {
          int16_t rawT = d[3] | ((int16_t)d[4]<<8);
          raumTemp = rawT / 10.0f - 50.0f;
        }
        if (d[1] != 0x00 && (sollTemp < 8 || sollTemp > 38)) hasError = true;
        if (d[1] != 0x00 && d[1] != 0x02 && d[1] != 0x08) hasUnknown = true;
        snprintf(buf, sizeof(buf),
          "Modus: %s | Soll: %.1f°C | Raum: %.1f°C | %s", mode, sollTemp, raumTemp, intensiv);
        result = buf;
      }
      break;

    case 0x3A:
      if (len >= 7) {
        float auslassTemp = (float)d[2] - 40.0f;
        float zwischTemp  = (float)d[3] - 40.0f;
        float sollTemp    = d[4] / 2.0f - 16.0f;
        const char* fehlerText = nullptr;
        bool isServiceCode = false;
        if (d[1] != 0x00) {
          switch (d[1]) {
            case 0x01: fehlerText = "Service fällig (gespeicherter Code)"; isServiceCode = true; hasUnknown = true; break;
            case 0x02: fehlerText = "HEIZUNG SPANNUNGSFEHLER (Klasse 2: Unterspannung)";  hasError = true; break;
            case 0x03: fehlerText = "HEIZUNG SPANNUNGSFEHLER (Klasse 3: Überspannung)";   hasError = true; break;
            case 0x04: fehlerText = "HEIZUNG KRAFTSTOFFFEHLER (Klasse 4)"; hasError = true; break;
            case 0x05: fehlerText = "WÄRMEKREISLAUF FEHLER (Klasse 5)"; hasError = true; break;
            case 0x06: fehlerText = "HEIZUNG DEFEKT (Klasse 6: Verriegelt)"; hasError = true; break;
            case 0x07: fehlerText = "TEMPERATURFEHLER HEIZUNG (Klasse 7)"; hasError = true; break;
            default: { static char fb[48]; snprintf(fb, sizeof(fb), "HEIZUNG FEHLER (B1=0x%02X)", d[1]); fehlerText = fb; hasError = true; hasUnknown = true; }
          }
        }
        if (d[0] != 0x9A) { hasError = true; hasUnknown = true; }
        const char* status = "UNBEKANNT";
        if      (d[6] == 0x00) status = "AUS";
        else if (d[6] == 0x20) status = "UMLUFT";
        else if (d[6] == 0x40) status = "NACHLAUF";
        else if (d[6] == 0x50) status = "HEIZT";
        else { hasError = true; hasUnknown = true; }
        const char* modus = "UNBEKANNT";
        if      (d[6] == 0x20)             modus = "UMLUFT";
        else if (d[6] == 0x00)             modus = "AUS";
        else if (lastHeizIntensiv == 0x80) modus = "INTENSIV";
        else                               modus = "COMFORT";
        if (auslassTemp < -40 || auslassTemp > 150) hasError = true;
        if (len >= 6 && d[5] != 0x00) hasUnknown = true;
        if (fehlerText) {
          snprintf(buf, sizeof(buf),
            "Status: %s | %s %s | Auslass: %.1f°C | Zwischen: %.1f°C | Soll: %.1f°C",
            status, isServiceCode ? "\xE2\x84\xB9" : "\xE2\x9A\xA0", fehlerText, auslassTemp, zwischTemp, sollTemp);
        } else {
          snprintf(buf, sizeof(buf),
            "Status: %s | Modus: %s | Auslass: %.1f°C | Zwischen: %.1f°C | Soll: %.1f°C",
            status, modus, auslassTemp, zwischTemp, sollTemp);
        }
        result = buf;
      }
      break;

    case 0x38:
      hasError = true;
      snprintf(buf, sizeof(buf), "ARTEFAKT: Break+Sync-Überlauf (0x%02X 0x%02X...)", d[0], len>1?d[1]:0);
      result = buf;
      break;

    case 0x3C:
      if (len >= 8) {
        snprintf(buf, sizeof(buf),
          "Danhag Diagnose: %02X%02X%02X%02X | B4=%02X B5=%02X B6=%02X B7=%02X",
          d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
      } else {
        snprintf(buf, sizeof(buf), "Danhag Diagnose: RAW %d Bytes", len);
      }
      result = buf;
      break;

    case 0x3D:
      snprintf(buf, sizeof(buf),
        "Danhag Echtzeit: B0=%02X B1=%02X B2=%02X B3=%02X B4=%02X B5=%02X",
        d[0], len>1?d[1]:0, len>2?d[2]:0, len>3?d[3]:0, len>4?d[4]:0, len>5?d[5]:0);
      result = buf;
      break;

    default:
      result = "RAW: ";
      for (uint8_t i = 0; i < len; i++) {
        char hexb[4]; snprintf(hexb, sizeof(hexb), "%02X ", d[i]);
        result += hexb;
      }
      hasUnknown = true;
      break;
  }
  return result;
}

// ============================================================
// GLOBALE ZÄHLER & STATUS
// ============================================================
uint32_t framesTotal      = 0;
uint32_t totalLoggedCount = 0;
uint32_t framesPerSec = 0;
uint32_t frameCounter = 0;
unsigned long lastStatTime = 0;
String   busStatus    = "WARTE AUF DATEN";

// ============================================================
// FRAME VERARBEITUNG
// ============================================================
void processCompleteFrame(uint8_t id, uint8_t* data, uint8_t len, uint8_t crc, bool crcOk) {
  if (id > 0x3F) return;
  if (id == 0x02 && len >= 2 && data[0] == 0x00 && data[1] == 0x55) {
    Serial.printf("[ARTEFAKT] 0x02 B0=00 B1=55 verworfen\n");
    return;
  }

  bool changed = false;
  if (frameStates[id].valid) {
    if (frameStates[id].dataLen != len) changed = true;
    else for (uint8_t i = 0; i < len; i++) {
      if (frameStates[id].data[i] != data[i]) { changed = true; break; }
    }
  } else {
    changed = true;
  }

  frameStates[id].lastSeen = millis();
  frameStates[id].count++;

  unsigned long deltaMs = 0;
  if (frameStates[id].valid && frameStates[id].lastLogged > 0)
    deltaMs = millis() - frameStates[id].lastLogged;

  memcpy(frameStates[id].data, data, min(len, (uint8_t)MAX_FRAME_BYTES));
  frameStates[id].dataLen = len;
  frameStates[id].valid   = true;

  frameCounter++;
  framesTotal++;

  bool hasError   = false;
  bool hasUnknown = false;
  String decoded = decodeFrame(id, data, len, hasError, hasUnknown);

  updateStats(id, data, len);

  if (hasError && changed) nvsWriteError(millis(), id, data, len, decoded);
  if (hasUnknown)          nvsWriteUnknown(millis(), id, data, len, decoded);

  bool doLog = false;
  if (isIBSFrame(id)) {
    if (millis() - frameStates[id].lastLogged >= IBS_LOG_INTERVAL_MS) {
      doLog = true;
      frameStates[id].lastLogged = millis();
    }
  } else {
    bool periodic = (millis() - frameStates[id].lastLogged >= 30000UL);
    doLog = changed || periodic;
    if (doLog) frameStates[id].lastLogged = millis();
  }

  if (doLog) {
    portENTER_CRITICAL(&logMux);
    LogEntry& e = logEntries[logHead];
    e.timestamp = millis();
    e.deltaMs   = deltaMs;
    e.id        = id;
    e.dataLen   = min(len, (uint8_t)MAX_FRAME_BYTES);
    memcpy(e.data, data, e.dataLen);
    e.crc         = crc;
    e.crcOk       = crcOk;
    e.changed     = changed;
    e.hasError    = hasError;
    e.hasUnknown  = hasUnknown;
    e.decoded     = decoded;
    logHead = (logHead + 1) % MAX_LOG_ENTRIES;
    if (logCount < MAX_LOG_ENTRIES) logCount++;
    totalLoggedCount++;
    portEXIT_CRITICAL(&logMux);
  }
}

// ============================================================
// LIN BUS EMPFANG (State Machine)
// ============================================================
enum RxState { WAIT_BREAK, WAIT_SYNC, WAIT_PID, COLLECT_DATA };
RxState  rxState     = WAIT_BREAK;
uint8_t  rxBuf[MAX_FRAME_BYTES];
uint8_t  rxLen       = 0;
uint8_t  currentID   = 0xFF;
uint8_t  expectedLen = 8;
unsigned long lastByteTime = 0;
extern unsigned long lastKuehlRepeat;
extern unsigned long lastHeizRepeat;
#define FRAME_TIMEOUT_MS  15

// ============================================================
// LIN-BUS SENDEN (TX)
// ============================================================
#define CMD_MIN_INTERVAL_MS    200
#define CMD_BUS_QUIET_MS        75

bool masterModeActive = true;

struct CtrlState {
  uint8_t heizModus;
  uint8_t heizSoll;
  uint8_t heizIntensiv;
  uint8_t kuehlStufe;
  uint8_t kuehlLastStufe;
  bool    pumpeAn;
  bool    lichtAn;
  uint8_t lichtHell;
  unsigned long lichtCmdTime;
  unsigned long heizCmdTime;
  unsigned long kuehlCmdTime;
  unsigned long lastCmdTime;
  bool ctrlActive;
} ctrl;

uint8_t linChecksum(uint8_t pid, uint8_t* data, uint8_t len) {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < len; i++) { sum += data[i]; if (sum > 0xFF) sum -= 0xFF; }
  return (uint8_t)(~sum & 0xFF);
}

uint8_t linChecksumEnhanced(uint8_t pid, uint8_t* data, uint8_t len) {
  uint16_t sum = pid;
  for (uint8_t i = 0; i < len; i++) { sum += data[i]; if (sum > 0xFF) sum -= 0xFF; }
  return (uint8_t)(~sum & 0xFF);
}

uint8_t linChecksumHeizCmd(uint8_t* data, uint8_t len) {
  uint16_t sum = 0x39;
  for (uint8_t i = 0; i < len; i++) { sum += data[i]; if (sum > 0xFF) sum -= 0xFF; }
  return (uint8_t)(~sum & 0xFF);
}

bool isEnhancedFrame(uint8_t id) {
  return (id == 0x01 || id == 0x02 || id == 0x04 || id == 0x05 ||
          id == 0x1B || id == 0x3B ||
          id == 0x20 ||  // Votronic Solar: Enhanced CRC (LIN 2.x)
          id == 0x22 || id == 0x25 || id == 0x26 || id == 0x3A);
}

uint8_t linChecksumAuto(uint8_t id, uint8_t pid, uint8_t* data, uint8_t len) {
  if (id == 0x39) return linChecksumHeizCmd(data, len);
  return isEnhancedFrame(id) ? linChecksumEnhanced(pid, data, len) : linChecksum(pid, data, len);
}

uint8_t linPID(uint8_t id) {
  uint8_t p0 = ((id>>0)^(id>>1)^(id>>2)^(id>>4)) & 0x01;
  uint8_t p1 = (~((id>>1)^(id>>3)^(id>>4)^(id>>5))) & 0x01;
  return (id & 0x3F) | (p0 << 6) | (p1 << 7);
}

bool linSendFrame(uint8_t id, uint8_t* data, uint8_t len, bool logTx = true, bool skipWait = false) {
  if (!skipWait) {
    unsigned long waitStart = millis();
    while ((millis() - lastByteTime) < CMD_BUS_QUIET_MS) {
      if (millis() - waitStart > 500) { Serial.println("[TX] ABBRUCH: Bus nicht idle"); return false; }
      delay(2);
    }
  }
  while (Serial2.available()) Serial2.read();
  Serial2.flush();
  uart_set_baudrate(UART_NUM_2, LIN_BAUD / 2);
  Serial2.write((uint8_t)0x00);
  Serial2.flush();
  uart_set_baudrate(UART_NUM_2, LIN_BAUD);
  delayMicroseconds(200);
  uint8_t pid = linPID(id);
  Serial2.write(0x55);
  Serial2.write(pid);
  for (uint8_t i = 0; i < len; i++) Serial2.write(data[i]);
  Serial2.write(linChecksumAuto(id, pid, data, len));
  Serial2.flush();
  uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(10));
  uart_flush_input(UART_NUM_2);
  if (rxState != COLLECT_DATA) {
    rxState = WAIT_BREAK; rxLen = 0; currentID = 0xFF; expectedLen = 8;
  }
  lastByteTime = millis();
  char txbuf[SERIAL_LOG_WIDTH]; int tpos = 0;
  tpos += snprintf(txbuf+tpos, sizeof(txbuf)-tpos, "[TX] 0x%02X PID=%02X: ", id, pid);
  for (uint8_t i = 0; i < len && tpos < (int)sizeof(txbuf)-4; i++)
    tpos += snprintf(txbuf+tpos, sizeof(txbuf)-tpos, "%02X ", data[i]);
  snprintf(txbuf+tpos, sizeof(txbuf)-tpos, "CS=%02X (%s)",
           linChecksumAuto(id, pid, data, len), id == 0x39 ? "HeizCmd" : isEnhancedFrame(id) ? "Enh" : "Cls");
  sLogLn("%s", txbuf);
  if (logTx) ctrl.lastCmdTime = millis();
  if (logTx) {
    bool hasErr = false, hasUnk = false;
    String decoded = "[TX] " + decodeFrame(id, data, len, hasErr, hasUnk);
    portENTER_CRITICAL(&logMux);
    LogEntry& txEntry = logEntries[logHead];
    txEntry.timestamp = millis(); txEntry.deltaMs = 0; txEntry.id = id;
    txEntry.dataLen = min(len, (uint8_t)MAX_FRAME_BYTES);
    memcpy(txEntry.data, data, txEntry.dataLen);
    txEntry.crc = linChecksumAuto(id, pid, data, len); txEntry.crcOk = true;
    txEntry.changed = true; txEntry.hasError = false; txEntry.hasUnknown = false;
    txEntry.decoded = decoded;
    logHead = (logHead + 1) % MAX_LOG_ENTRIES;
    if (logCount < MAX_LOG_ENTRIES) logCount++;
    totalLoggedCount++;
    portEXIT_CRITICAL(&logMux);
  }
  return true;
}

uint8_t getFrameDataLen(uint8_t id);
void processLinByte(uint8_t b);

void linSendHeader(uint8_t id) {
  uint8_t pid = linPID(id);
  Serial2.flush();
  uart_set_baudrate(UART_NUM_2, LIN_BAUD / 2);
  Serial2.write((uint8_t)0x00);
  Serial2.flush();
  uart_set_baudrate(UART_NUM_2, LIN_BAUD);
  delayMicroseconds(200);
  Serial2.write(0x55);
  Serial2.write(pid);
  Serial2.flush();
  uart_wait_tx_done(UART_NUM_2, pdMS_TO_TICKS(5));
  unsigned long t = millis();
  int echoBytesRead = 0;
  while (echoBytesRead < 3 && (millis() - t) < 6) {
    if (Serial2.available()) { Serial2.read(); echoBytesRead++; }
  }
  lastByteTime = millis();
  rxState     = COLLECT_DATA;
  rxLen       = 0;
  currentID   = id;
  expectedLen = getFrameDataLen(id);
  delay(8);
  sLogLn("[POLL] 0x%02X PID=%02X expLen=%d", id, pid, expectedLen);
}

// ============================================================
// MASTER-SCHEDULER
// Slot 0: 0x01 Kühlbox/Pumpe
// Slot 1: 0x02 Sensor
// Slot 2: 0x05 Licht
// Slot 3: 0x3B LED
// Slot 4: 0x04 Kühlbox-Status
// Slot 5-8 (alle 5 Zyklen): 0x22, 0x26, 0x25, 0x20 Solar NEU
// Slot 9-10 (alle 10 Zyklen): 0x3A Heizung Status
// ============================================================
#define MASTER_CYCLE_MS   350UL
#define MASTER_SLOT_MS     20UL

static unsigned long lastMasterCycle  = 0;
static unsigned long lastSlotTime     = 0;
static uint8_t       masterCycleCount = 0;
static uint8_t       masterSlot       = 0xFF;

bool linSendDisplay(bool logTx = true);
bool linSendLicht(bool an, uint8_t helligkeit, bool logTx = true);

void linMasterTick() {
  if (!masterModeActive) return;
  unsigned long now = millis();
  if ((now - lastMasterCycle) >= MASTER_CYCLE_MS) {
    lastMasterCycle = now;
    lastSlotTime    = now;
    masterCycleCount++;
    masterSlot = 0;
  }
  if (masterSlot == 0xFF) return;
  if ((now - lastSlotTime) < MASTER_SLOT_MS) return;
  lastSlotTime = now;

  switch (masterSlot) {
    case 0: linSendDisplay(false); masterSlot = 1; break;
    case 1: linSendHeader(0x02);   masterSlot = 2; break;
    case 2: { uint8_t hell = ctrl.lichtHell ? ctrl.lichtHell : 5; linSendLicht(ctrl.lichtAn, hell, false); masterSlot = 3; break; }
    case 3: linSendHeader(0x3B);   masterSlot = 4; break;
    case 4:
      linSendHeader(0x04);
      masterSlot = (masterCycleCount % 5 == 0) ? 5 : 0xFF;
      break;
    case 5: linSendHeader(0x22); masterSlot = 6; break;
    case 6: linSendHeader(0x26); masterSlot = 7; break;
    case 7: linSendHeader(0x25); masterSlot = 8; break;
    // NEU v9.1: Votronic Solar 0x20 in jeden 5. Zyklus eingebaut
    case 8:
      linSendHeader(0x20);
      masterSlot = (masterCycleCount % 5 == 0) ? 9 : 0xFF;
      break;
    case 9:
      linSendHeader(0x21);  // NEU v9.6: Wasserstand alternativ
      masterSlot = (masterCycleCount % 10 == 0) ? 10 : 0xFF;
      break;
    case 10: linSendHeader(0x3A); masterSlot = 11; break;
    case 11: linSendHeader(0x18); masterSlot = 12; break;
    case 12: linSendHeader(0x19); masterSlot = 0xFF; break;
    default: masterSlot = 0xFF; break;
  }
}

bool linSendHeizung(uint8_t modus, uint8_t sollRaw, uint8_t intensiv, int16_t raumRaw) {
  if (intensiv != 0x00 && intensiv != 0x80) intensiv = 0x00;
  uint8_t d[8];
  d[0] = 0xFF; d[1] = modus; d[2] = sollRaw;
  d[3] = raumRaw & 0xFF; d[4] = (raumRaw >> 8) & 0xFF;
  d[5] = 0x00; d[6] = 0x00; d[7] = intensiv;
  ctrl.heizModus = modus; ctrl.heizSoll = sollRaw;
  ctrl.heizIntensiv = intensiv; ctrl.heizCmdTime = millis();
  ctrl.ctrlActive = true;
  if (modus != 0x00) lastHeizRepeat = 0;
  return linSendFrame(0x39, d, 8);
}

// Touch-Bit Timer — aktiviert Wasserstand-Sensor in Westfalia-Elektronik
// Original-Display sendet B0=0x01 fast dauerhaft — alle 15s kurz AUS fuer 350ms
static unsigned long lastTouchBitTime  = 0;
static bool          touchBitActive    = true;  // Beim Start sofort AN
#define TOUCH_BIT_INTERVAL_MS  15000UL
#define TOUCH_BIT_DURATION_MS    350UL

bool linSendDisplay(bool logTx) {
  uint8_t d[8] = {0};
  if (ctrl.pumpeAn) d[0] |= 0x02;
  d[1] = (ctrl.kuehlStufe & 0x07) << 4;

  // Touch-Bit (B0=0x01) — aktiviert Wasserstand-Sensor
  // Original-Display: fast dauerhaft AN, alle 15s kurz AUS fuer 350ms
  unsigned long now = millis();
  if (!touchBitActive && (now - lastTouchBitTime) >= TOUCH_BIT_INTERVAL_MS) {
    touchBitActive   = true;
    lastTouchBitTime = now;
  }
  if (touchBitActive) {
    d[0] |= 0x01;
    if ((now - lastTouchBitTime) >= TOUCH_BIT_DURATION_MS) {
      touchBitActive = false;
    }
  }

  return linSendFrame(0x01, d, 8, logTx, true);
}

bool linSendKuehlbox(uint8_t stufe) {
  if (stufe > 0) ctrl.kuehlLastStufe = stufe;
  ctrl.kuehlStufe   = stufe;
  ctrl.kuehlCmdTime = millis();
  ctrl.ctrlActive   = true;
  if (stufe > 0) lastKuehlRepeat = 0;
  return linSendDisplay();
}

bool linSendLicht(bool an, uint8_t helligkeit, bool logTx) {
  if (helligkeit < 1) helligkeit = 1;
  if (helligkeit > 10) helligkeit = 10;
  if (logTx) {
    ctrl.lichtAn      = an;
    ctrl.lichtHell    = helligkeit;
    ctrl.lichtCmdTime = millis();
    ctrl.ctrlActive   = true;
  }
  uint8_t d[8] = {0};
  d[0] = an ? 0x01 : 0x00;
  if (logTx) d[0] |= 0x80;  // Touch-Bit bei User-Befehl (Original-Display Verhalten)
  d[1] = (0x05 << 4) | helligkeit;  // Farbton fix 5, Helligkeit ins lower Nibble
  return linSendFrame(0x05, d, 8, logTx, true);
}

void ctrlWatchdog() {
  if (!ctrl.pumpeAn) return;
}

#define KUEHLBOX_REPEAT_MS   300UL
unsigned long lastKuehlRepeat = 0;

void kuehlboxRepeatTx() {
  if (ctrl.kuehlStufe == 0 || ctrl.kuehlCmdTime == 0) return;
  if (rxState == COLLECT_DATA) return;
  if ((millis() - lastKuehlRepeat) < KUEHLBOX_REPEAT_MS) return;
  lastKuehlRepeat = millis();
  linSendDisplay(false);
}

#define HEIZUNG_REPEAT_MS  5000UL
unsigned long lastHeizRepeat = 0;
void heizungRepeatTx() {
  if (ctrl.heizModus == 0x00 || ctrl.heizCmdTime == 0) return;
  if (rxState == COLLECT_DATA) return;
  if ((millis() - lastHeizRepeat) < HEIZUNG_REPEAT_MS) return;
  lastHeizRepeat = millis();
  int16_t raumRaw = 700;
  if (frameStates[0x02].valid && frameStates[0x02].dataLen >= 2 &&
      !(frameStates[0x02].data[0] == 0 && frameStates[0x02].data[1] == 0)) {
    float tInnen = (120.0f - frameStates[0x02].data[0]) / 2.0f;
    raumRaw = (int16_t)((tInnen - 2.0f + 50.0f) * 10.0f);  // -2.0°C Offset: Heizung springt früher an
  }
  uint8_t d[8];
  uint8_t safeIntensiv = (ctrl.heizIntensiv == 0x80) ? 0x80 : 0x00;
  d[0] = 0xFF; d[1] = ctrl.heizModus; d[2] = ctrl.heizSoll;
  d[3] = raumRaw & 0xFF; d[4] = (raumRaw >> 8) & 0xFF;
  d[5] = 0x00; d[6] = 0x00; d[7] = safeIntensiv;
  linSendFrame(0x39, d, 8, false, true);
}

WebServer server(80);

const char* getFrameName(uint8_t id) {
  for (uint8_t i = 0; i < FRAME_DEF_COUNT; i++)
    if (FRAME_DEFS[i].id == id) return FRAME_DEFS[i].name;
  return "UNBEKANNT";
}

uint8_t getFrameDataLen(uint8_t id) {
  for (uint8_t i = 0; i < FRAME_DEF_COUNT; i++)
    if (FRAME_DEFS[i].id == id) return FRAME_DEFS[i].dataLen;
  return 8;
}

bool linCheckPID(uint8_t pid) {
  uint8_t id = pid & 0x3F;
  uint8_t p0 = ((id>>0)^(id>>1)^(id>>2)^(id>>4)) & 0x01;
  uint8_t p1 = (~((id>>1)^(id>>3)^(id>>4)^(id>>5))) & 0x01;
  return pid == ((id) | (p0<<6) | (p1<<7));
}

void processLinByte(uint8_t b) {
  unsigned long now = millis();
  if (rxState != WAIT_BREAK && (now - lastByteTime) > FRAME_TIMEOUT_MS) {
    if (rxState == COLLECT_DATA && rxLen > 1) parserStats.rxTimeout++;
    rxState = WAIT_BREAK; rxLen = 0; currentID = 0xFF; expectedLen = 8;
  }
  lastByteTime = now;
  switch (rxState) {
    case WAIT_BREAK: if (b == 0x00) rxState = WAIT_SYNC; break;
    case WAIT_SYNC:
      if      (b == 0x55) { rxState = WAIT_PID; rxLen = 0; currentID = 0xFF; }
      else if (b == 0x00)   break;
      else                  rxState = WAIT_BREAK;
      break;
    case WAIT_PID:
      currentID   = b & 0x3F;
      expectedLen = getFrameDataLen(currentID);
      if (!linCheckPID(b)) parserStats.rxParityError++;
      if (expectedLen == 0) { rxState = WAIT_BREAK; rxLen = 0; currentID = 0xFF; expectedLen = 8; }
      else                  { rxState = COLLECT_DATA; rxLen = 0; }
      break;
    case COLLECT_DATA:
      if (rxLen < MAX_FRAME_BYTES) { rxBuf[rxLen++] = b; }
      else { parserStats.rxLengthOverflow++; rxState = WAIT_BREAK; rxLen = 0; currentID = 0xFF; expectedLen = 8; break; }
      {
        bool frameDone = false;
        if (currentID == 0x04) frameDone = (rxLen == 4) || (rxLen == 9);
        else                   frameDone = (rxLen >= (uint8_t)(expectedLen + 1));
        if (frameDone) {
          uint8_t pid     = linPID(currentID);
          uint8_t crcRx   = rxBuf[rxLen - 1];
          uint8_t crcCalc = linChecksumAuto(currentID, pid, rxBuf, rxLen - 1);
          bool    crcOk   = (crcRx == crcCalc);
          if (!crcOk) parserStats.rxParityError++;
          processCompleteFrame(currentID, rxBuf, rxLen - 1, crcRx, crcOk);
          rxState = WAIT_BREAK; rxLen = 0; currentID = 0xFF; expectedLen = 8;
        }
      }
      break;
  }
}

// ============================================================
// HTTP-HANDLER: Steuerungsbefehle
// ============================================================
bool cmdSafetyCheck(String& reason) {
  if (!masterModeActive && busStatus != "AKTIV") { reason = "Bus nicht aktiv"; return false; }
  if ((millis() - ctrl.lastCmdTime) < CMD_MIN_INTERVAL_MS) { reason = "Zu schnell"; return false; }
  return true;
}

void handleCmdHeizung() {
  String reason;
  if (!cmdSafetyCheck(reason)) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\""+reason+"\"}"); return; }
  String modusStr = server.arg("modus");
  int    sollDeg  = server.arg("soll").toInt();
  bool   intensiv = server.arg("intensiv") == "1";
  uint8_t modus = 0x00;
  if      (modusStr == "an")     modus = 0x02;
  else if (modusStr == "umluft") modus = 0x08;
  else if (modusStr == "aus")    modus = 0x00;
  else { server.send(400, "application/json", "{\"ok\":false,\"reason\":\"Unbekannter Modus\"}"); return; }
  if (modus != 0x00 && (sollDeg < 8 || sollDeg > 38)) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\"Solltemperatur 8-38 Grad\"}"); return; }
  uint8_t sollRaw = (uint8_t)((sollDeg + 16) * 2);
  int16_t raumRaw = 700;
  if (frameStates[0x02].valid && frameStates[0x02].dataLen >= 2 &&
      !(frameStates[0x02].data[0] == 0 && frameStates[0x02].data[1] == 0)) {
    float tInnen = (120.0f - frameStates[0x02].data[0]) / 2.0f;
    raumRaw = (int16_t)((tInnen - 2.0f + 50.0f) * 10.0f);  // -2.0°C Offset: Heizung springt früher an
  }
  bool ok = linSendHeizung(modus, sollRaw, intensiv ? 0x80 : 0x00, raumRaw);
  char resp[200]; snprintf(resp, sizeof(resp), "{\"ok\":%s,\"modus\":\"%s\",\"soll\":%d}", ok?"true":"false", modusStr.c_str(), sollDeg);
  server.send(200, "application/json", resp);
}

void handleCmdMaster() {
  String an = server.arg("an");
  masterModeActive = (an == "1");
  if (!masterModeActive) { ctrl.kuehlStufe = 0; ctrl.pumpeAn = false; lastMasterCycle = 0; masterCycleCount = 0; }
  char resp[60]; snprintf(resp, sizeof(resp), "{\"ok\":true,\"master\":%s}", masterModeActive?"true":"false");
  server.send(200, "application/json", resp);
}

void handleCmdPumpe() {
  String reason;
  if (!cmdSafetyCheck(reason)) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\""+reason+"\"}"); return; }
  ctrl.pumpeAn = (server.arg("an") == "1");
  bool ok = linSendDisplay();
  char resp[60]; snprintf(resp, sizeof(resp), "{\"ok\":%s,\"pumpe\":%s}", ok?"true":"false", ctrl.pumpeAn?"true":"false");
  server.send(200, "application/json", resp);
}

void handleCmdKuehlbox() {
  String reason;
  if (!cmdSafetyCheck(reason)) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\""+reason+"\"}"); return; }
  int stufe = server.arg("stufe").toInt();
  if (stufe < 0 || stufe > 5) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\"Stufe 0-5\"}"); return; }
  bool ok = linSendKuehlbox((uint8_t)stufe);
  char resp[80]; snprintf(resp, sizeof(resp), "{\"ok\":%s,\"stufe\":%d}", ok?"true":"false", stufe);
  server.send(200, "application/json", resp);
}

void handleCmdLicht() {
  String reason;
  if (!cmdSafetyCheck(reason)) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\""+reason+"\"}"); return; }
  bool an = (server.arg("an") == "1");
  int hell = server.arg("helligkeit").toInt();
  if (hell == 0) hell = ctrl.lichtHell ? ctrl.lichtHell : 5;
  if (hell < 1 || hell > 10) { server.send(400, "application/json", "{\"ok\":false,\"reason\":\"Helligkeit 1-10\"}"); return; }
  bool ok = linSendLicht(an, (uint8_t)hell);
  char resp[80]; snprintf(resp, sizeof(resp), "{\"ok\":%s,\"an\":%s,\"helligkeit\":%d}", ok?"true":"false", an?"true":"false", hell);
  server.send(200, "application/json", resp);
}

void handleCmdRestart() {
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"ESP32 startet neu...\"}");
  delay(200); ESP.restart();
}

void handleCmdStatus() {
  float heizSollDeg = ctrl.heizSoll / 2.0f - 16.0f;
  unsigned long pumpeRemain = 0;
  char resp[220];
  snprintf(resp, sizeof(resp),
    "{\"heizModus\":%d,\"heizSoll\":%.1f,\"kuehlStufe\":%d,\"pumpeAn\":%s,\"pumpeAutoOffSek\":%lu,\"lichtAn\":%s,\"lichtHell\":%d}",
    ctrl.heizModus, heizSollDeg, ctrl.kuehlStufe,
    ctrl.pumpeAn?"true":"false", pumpeRemain, ctrl.lichtAn?"true":"false", ctrl.lichtHell);
  server.send(200, "application/json", resp);
}

// ============================================================
// WEBSERVER: Serial-Log
// ============================================================
void handleSerial() {
  String html = F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='3'><title>Serial Log - Nugget v9.1</title>"
    "<style>body{font-family:monospace;background:#1a1a2e;color:#eee;margin:0;padding:10px;}"
    "h2{color:#e94560;}pre{background:#0f3460;padding:10px;border-radius:6px;font-size:12px;"
    "white-space:pre-wrap;max-height:85vh;overflow-y:auto;}"
    ".info{color:#888;font-size:11px;margin-bottom:6px;}a{color:#e94560;}</style></head><body>"
    "<h2>🖥 Serial Log</h2><div class='info'>Auto-Refresh 3s &mdash; <a href='/'>← Zurück</a></div><pre>");
  portENTER_CRITICAL(&serialLogMux);
  uint8_t start = (serialLogCount < SERIAL_LOG_LINES) ? 0 : serialLogHead;
  for (uint8_t i = 0; i < serialLogCount; i++)
    html += String(serialLogBuf[(start + i) % SERIAL_LOG_LINES]);
  portEXIT_CRITICAL(&serialLogMux);
  html += F("</pre></body></html>");
  server.send(200, "text/html", html);
}

// ============================================================
// OTA Browser-Update
// ============================================================
void handleUpdateGet() {
  server.send(200, "text/html", R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>OTA Update - Nugget v9.9</title>
<style>body{font-family:monospace;background:#1a1a2e;color:#eee;padding:30px;}
h1{color:#e94560;}.box{background:#16213e;border-radius:8px;padding:24px;max-width:480px;}
p{color:#aaa;font-size:13px;}input[type=file]{color:#eee;margin:12px 0;display:block;}
button{background:#0f3460;color:#eee;border:none;padding:10px 20px;border-radius:4px;cursor:pointer;}
button:hover{background:#e94560;}#progress{display:none;margin-top:16px;}
progress{width:100%;height:20px;}#msg{margin-top:12px;font-size:13px;}
.back{color:#e94560;text-decoration:none;font-size:13px;display:inline-block;margin-bottom:20px;}</style>
</head><body>
<a class="back" href="/">← Zurück</a>
<h1>⬆️ OTA Firmware Update</h1>
<div class="box">
  <p>Neue Firmware hochladen ohne USB-Kabel.<br>
     Arduino IDE: <b>Sketch → Export Compiled Binary</b> → <code>.bin</code> auswählen.</p>
  <p style="color:#7fff7f;font-size:12px">✅ Rollback aktiv: Bei fehlerhafter Firmware bootet ESP32 automatisch zurück.</p>
  <form id="f" method="POST" action="/update" enctype="multipart/form-data">
    <input type="file" id="bin" name="firmware" accept=".bin" required>
    <button type="submit">Firmware flashen</button>
  </form>
  <div id="progress"><p>Upload läuft...</p><progress id="bar" value="0" max="100"></progress><div id="msg"></div></div>
</div>
<script>
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  const file=document.getElementById('bin').files[0]; if(!file) return;
  const fd=new FormData(); fd.append('firmware',file);
  document.getElementById('progress').style.display='block';
  document.querySelector('button').disabled=true;
  document.getElementById('msg').textContent='Uploading '+file.name+'...';
  const xhr=new XMLHttpRequest(); xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(e){if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);document.getElementById('bar').value=p;document.getElementById('msg').textContent=p+'%...';}};
  xhr.onload=function(){if(xhr.status===200){document.getElementById('msg').textContent='✅ OK! Neustart...';setTimeout(()=>{window.location.href='/';},5000);}else{document.getElementById('msg').textContent='❌ Fehler: '+xhr.responseText;document.querySelector('button').disabled=false;}};
  xhr.onerror=function(){document.getElementById('msg').textContent='❌ Verbindungsfehler';};
  xhr.send(fd);});
</script></body></html>)rawhtml");
}

void handleUpdatePost() {
  if (Update.hasError()) server.send(500, "text/plain", "Update fehlgeschlagen: " + String(Update.errorString()));
  else { server.send(200, "text/plain", "OK"); delay(500); ESP.restart(); }
}

void handleUpdateUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("[OTA] Start: %s\n", upload.filename.c_str());
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    Update.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("[OTA] OK: %u Bytes\n", upload.totalSize);
  }
}

// ============================================================
// WEBSERVER: Haupt-HTML
// ============================================================
// ============================================================
// WEBSERVER: LIN Scanner
// ============================================================
void handleScanStart() {
  if (scanActive) {
    server.send(200, "application/json", "{\"ok\":false,\"msg\":\"Laeuft bereits\"}");
    return;
  }
  memset(scanResults, 0, sizeof(scanResults));
  scanActive       = true;
  scanDone         = false;
  bool wasMaster   = masterModeActive;
  masterModeActive = false;  // Master pausieren
  Serial.printf("[SCAN] Start: 0x%02X-0x%02X\n", SCAN_ID_MIN, SCAN_ID_MAX);

  for (uint8_t id = SCAN_ID_MIN; id <= SCAN_ID_MAX; id++) {
    scanCurrentId = id;
    // Master-Frames ueberspringen
    if (id == 0x01 || id == 0x05 || id == 0x39) { delay(20); continue; }

    // Puffer leeren
    while (Serial2.available()) Serial2.read();

    // Header senden
    linSendHeader(id);

    // Auf Response warten (max 35ms)
    uint8_t buf[12]; uint8_t len = 0;
    unsigned long t = millis();
    while (millis() - t < 35 && len < 11) {
      if (Serial2.available()) buf[len++] = Serial2.read();
    }

    scanResults[id].id = id;
    if (len > 1) {
      scanResults[id].responded = true;
      uint8_t dl = (len - 1 > 10) ? 10 : len - 1;
      memcpy(scanResults[id].data, buf, dl);
      scanResults[id].dataLen = dl;
      scanResults[id].crc     = buf[len - 1];
      Serial.printf("[SCAN] 0x%02X: ", id);
      for (uint8_t b = 0; b < dl; b++) Serial.printf("%02X ", buf[b]);
      Serial.printf("CRC:%02X\n", scanResults[id].crc);
    } else {
      scanResults[id].responded = false;
    }
    delay(30);  // kurz warten zwischen IDs
  }

  masterModeActive = wasMaster;  // Master wiederherstellen
  scanActive       = false;
  scanDone         = true;
  scanCurrentId    = SCAN_ID_MAX + 1;
  Serial.println("[SCAN] Abgeschlossen.");

  // Direkt Ergebnis zurueckgeben
  String json = "{\"ok\":true,\"done\":true,\"results\":[";
  bool first = true;
  for (uint8_t i = SCAN_ID_MIN; i <= SCAN_ID_MAX; i++) {
    if (!scanResults[i].responded) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"id\":\"0x"; json += String(i, HEX); json += "\",\"bytes\":\"";
    char hex[4];
    for (uint8_t b = 0; b < scanResults[i].dataLen; b++) {
      snprintf(hex, sizeof(hex), "%02X ", scanResults[i].data[b]);
      json += hex;
    }
    json += "\",\"crc\":\""; json += String(scanResults[i].crc, HEX); json += "\"}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleScanStatus() {
  String json = "{";
  json += "\"active\":"; json += scanActive ? "true" : "false"; json += ",";
  json += "\"done\":";   json += scanDone   ? "true" : "false"; json += ",";
  json += "\"current\":\"0x"; json += String(scanCurrentId, HEX); json += "\",";
  json += "\"results\":[";
  bool first = true;
  for (uint8_t i = SCAN_ID_MIN; i <= SCAN_ID_MAX; i++) {
    if (!scanResults[i].responded) continue;
    if (!first) json += ",";
    first = false;
    json += "{\"id\":\"0x"; json += String(i, HEX); json += "\",\"bytes\":\"";
    char hex[4];
    for (uint8_t b = 0; b < scanResults[i].dataLen; b++) {
      snprintf(hex, sizeof(hex), "%02X ", scanResults[i].data[b]);
      json += hex;
    }
    json += "\",\"crc\":\""; json += String(scanResults[i].crc, HEX); json += "\"}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(R"rawhtml(<!DOCTYPE html>
<html><head><meta charset="UTF-8"><title>Nugget LIN Sniffer v9.1</title>
<style>
  body{font-family:monospace;background:#1a1a2e;color:#eee;margin:0;padding:10px;}
  h1{color:#e94560;margin:0 0 10px 0;}
  .header{display:flex;gap:20px;align-items:center;flex-wrap:wrap;margin-bottom:10px;}
  .stat{background:#16213e;padding:8px 14px;border-radius:6px;font-size:13px;}.stat b{color:#e94560;}
  .controls{margin:8px 0;display:flex;gap:8px;flex-wrap:wrap;}
  button{background:#0f3460;color:#eee;border:none;padding:7px 14px;border-radius:4px;cursor:pointer;font-family:monospace;}
  button:hover{background:#e94560;}button.active{background:#e94560;}button.danger{background:#5a1a1a;}
  table{width:100%;border-collapse:collapse;font-size:12px;}
  th{background:#0f3460;color:#eee;padding:6px 8px;text-align:left;position:sticky;top:0;}
  td{padding:4px 8px;border-bottom:1px solid #16213e;vertical-align:top;}
  tr.changed{background:#1a2a1a;}tr.changed td{color:#7fff7f;}
  tr.error{background:#2a1a1a;}tr.error td{color:#ff7f7f;}
  tr.unknown{background:#1a1a2a;}tr.unknown td{color:#f0a500;}
  tr:hover{background:#16213e;}
  .id-cell{color:#e94560;font-weight:bold;}.name-cell{color:#f0a500;}
  .decoded-cell{color:#7fbfff;font-size:11px;}.ts-cell{color:#888;}
  #logTable{max-height:55vh;overflow-y:auto;display:block;}
  #nvsPanel{display:none;background:#16213e;border-radius:8px;padding:12px;margin:10px 0;}
  #scanPanel{display:none;background:#0a1628;border:1px solid #0f6460;border-radius:8px;padding:12px;margin:10px 0;}
  #scanPanel h3{color:#7fff7f;margin:0 0 8px 0;}
  .scan-row{padding:3px 0;border-bottom:1px solid #0f3460;font-family:monospace;font-size:12px;}
  .scan-id{color:#f0a500;font-weight:bold;display:inline-block;min-width:50px;}
  .scan-bytes{color:#7fbfff;}
  .scan-crc{color:#7fff7f;}
  #nvsPanel h3{color:#f0a500;margin:0 0 8px 0;}#nvsContent{max-height:40vh;overflow-y:auto;}
  .bus-active{color:#7fff7f;}.bus-inactive{color:#ff7f7f;}
  .live-title{color:#f0a500;font-size:14px;font-weight:bold;margin-bottom:8px;}
  .live-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:8px;margin-bottom:12px;}
  .live-card{background:#16213e;border-radius:8px;padding:10px 12px;border-left:3px solid #0f3460;transition:border-color 0.3s;}
  .live-card.active{border-left-color:#7fff7f;}.live-card.warning{border-left-color:#ff7f7f;}
  .live-card-title{font-size:12px;font-weight:bold;color:#f0a500;margin-bottom:6px;border-bottom:1px solid #0f3460;padding-bottom:4px;}
  .live-row{display:flex;justify-content:space-between;font-size:11px;padding:2px 0;}
  .live-label{color:#888;}.live-val{color:#eee;font-weight:bold;}
  .live-val.on{color:#7fff7f;}.live-val.off{color:#888;}.live-val.warn{color:#ff7f7f;}.live-val.neu{color:#f0a500;}
  .water-bar{display:inline-block;width:60px;height:8px;background:#0f3460;border-radius:4px;vertical-align:middle;margin-left:4px;overflow:hidden;}
  .water-fill{height:100%;border-radius:4px;transition:width 0.5s;}
  .water-fresh{background:#7fbfff;}.water-grey{background:#aaa;}
  .nvs-stat-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:8px;margin-bottom:12px;}
  .nvs-stat-item{background:#0f3460;border-radius:6px;padding:8px;font-size:12px;}
  .nvs-stat-label{color:#888;font-size:10px;}.nvs-stat-val{color:#f0a500;font-weight:bold;font-size:14px;}
  .ctrl-panel{background:#16213e;border-radius:10px;padding:14px 16px;margin:12px 0;border-left:4px solid #e94560;}
  .ctrl-panel h3{color:#e94560;margin:0 0 12px 0;font-size:15px;}
  .ctrl-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;}
  @media(max-width:700px){.ctrl-grid{grid-template-columns:1fr;}}
  .ctrl-section{background:#0f3460;border-radius:8px;padding:12px;}
  .ctrl-section h4{color:#f0a500;margin:0 0 10px 0;font-size:13px;}
  .ctrl-row{display:flex;align-items:center;gap:8px;margin:6px 0;font-size:12px;}
  .ctrl-row label{color:#888;min-width:90px;}
  .ctrl-row input[type=range]{flex:1;accent-color:#e94560;}
  .ctrl-row select{background:#16213e;border:1px solid #0f3460;color:#eee;padding:4px 6px;border-radius:4px;font-family:monospace;flex:1;}
  .ctrl-val{color:#f0a500;font-weight:bold;min-width:40px;text-align:right;}
  .ctrl-btn{padding:8px 16px;border-radius:5px;border:none;cursor:pointer;font-family:monospace;font-size:12px;font-weight:bold;}
  .ctrl-btn-send{background:#e94560;color:#fff;}.ctrl-btn-send:hover{background:#ff6b80;}
  .ctrl-btn-off{background:#5a1a1a;color:#ff7f7f;}.ctrl-btn-off:hover{background:#e94560;color:#fff;}
  .ctrl-status{font-size:11px;color:#888;margin-top:8px;min-height:16px;}
  .ctrl-status.ok{color:#7fff7f;}.ctrl-status.err{color:#ff7f7f;}
  .ctrl-warning{background:#2a1500;border:1px solid #f0a500;border-radius:6px;padding:8px 12px;font-size:11px;color:#f0a500;margin-bottom:10px;}
</style></head><body>
<h1>🚐 Ford Nugget LIN-Bus Sniffer <small style="color:#888;font-size:14px">v9.9</small></h1>
<div class="header">
  <div class="stat">Bus: <b id="busStatus" class="bus-inactive">--</b></div>
  <div class="stat">Frames/s: <b id="fps">0</b></div>
  <div class="stat">Gesamt: <b id="total">0</b></div>
  <div class="stat">WiFi: <b id="rssi">--</b> dBm</div>
  <div class="stat">Uptime: <b id="uptime">--</b></div>
  <div class="stat">NVS Fehler: <b id="nvsErrCnt" style="color:#ff7f7f">0</b></div>
  <div class="stat">NVS Unbekannt: <b id="nvsUnkCnt" style="color:#f0a500">0</b></div>
  <div class="stat">⚠ Parser: <b id="parserErrCnt" style="color:#f0a500">0</b></div>
  <div class="stat"><a href="/update" style="color:#7fff7f;text-decoration:none;font-weight:bold">⬆️ OTA Update</a></div>
  <div class="stat"><button onclick="cmdRestart()" style="background:#c0392b;color:#fff;border:none;border-radius:4px;padding:2px 10px;cursor:pointer;font-weight:bold">🔄 Neustart</button></div>
  <div class="stat"><button onclick="wifiReset()" style="background:#8e44ad;color:#fff;border:none;border-radius:4px;padding:2px 10px;cursor:pointer;font-weight:bold">📶 WLAN/MQTT Reset</button></div>
  <div class="stat"><button id="scanBtn" onclick="startScan()" style="background:#0f6460;color:#7fff7f;border:none;border-radius:4px;padding:2px 10px;cursor:pointer;font-weight:bold">🔍 LIN Scanner</button></div>
</div>
)rawhtml");

  server.sendContent(R"rawhtml(
<div id="liveStatus">
  <div class="live-title">📊 Live Gerätestatus</div>
  <div class="live-grid">
    <div class="live-card" id="card-heizung">
      <div class="live-card-title">🔥 Standheizung</div>
      <div class="live-row"><span class="live-label">Status</span><span class="live-val" id="ls-heiz-status">--</span></div>
      <div class="live-row"><span class="live-label">Modus</span><span class="live-val" id="ls-heiz-modus">--</span></div>
      <div class="live-row"><span class="live-label">Solltemp</span><span class="live-val" id="ls-heiz-soll">--</span></div>
      <div class="live-row"><span class="live-label">Raumtemp</span><span class="live-val" id="ls-heiz-raum">--</span></div>
      <div class="live-row"><span class="live-label">Auslass</span><span class="live-val" id="ls-heiz-auslass">--</span></div>
      <div class="live-row"><span class="live-label">Fehler</span><span class="live-val" id="ls-heiz-fehler">--</span></div>
    </div>
    <div class="live-card" id="card-kuehlbox">
      <div class="live-card-title">❄️ Kühlbox</div>
      <div class="live-row"><span class="live-label">Status</span><span class="live-val" id="ls-kuehl-status">--</span></div>
      <div class="live-row"><span class="live-label">Stufe</span><span class="live-val" id="ls-kuehl-stufe">--</span></div>
      <div class="live-row"><span class="live-label">Kompressor</span><span class="live-val" id="ls-kuehl-komp">--</span></div>
      <div class="live-row"><span class="live-label">Temperatur</span><span class="live-val" id="ls-kuehl-temp">--</span></div>
    </div>
    <div class="live-card" id="card-temp">
      <div class="live-card-title">🌡️ Temperaturen</div>
      <div class="live-row"><span class="live-label">Innen</span><span class="live-val" id="ls-temp-innen">--</span></div>
      <div class="live-row"><span class="live-label">Außen</span><span class="live-val" id="ls-temp-aussen">--</span></div>
    </div>
    <div class="live-card" id="card-wasser">
      <div class="live-card-title">💧 Wasser</div>
      <div class="live-row"><span class="live-label">Frischwasser</span><span class="live-val" id="ls-wasser-frisch">--</span></div>
      <div class="live-row"><span class="live-label">Grauwasser</span><span class="live-val" id="ls-wasser-grau">--</span></div>
      <div class="live-row"><span class="live-label">Pumpe</span><span class="live-val" id="ls-wasser-pumpe">--</span></div>
      <div class="live-row"><span class="live-label">Wasserhahn</span><span class="live-val" id="ls-wasser-hahn">--</span></div>
    </div>
    <div class="live-card" id="card-licht">
      <div class="live-card-title">💡 Licht</div>
      <div class="live-row"><span class="live-label">Status</span><span class="live-val" id="ls-licht-an">--</span></div>
      <div class="live-row"><span class="live-label">Helligkeit</span><span class="live-val" id="ls-licht-hell">--</span></div>
      <div class="live-row"><span class="live-label">Farbton</span><span class="live-val" id="ls-licht-ton">--</span></div>
      <div class="live-row"><span class="live-label">Quelle</span><span class="live-val" id="ls-licht-src">--</span></div>
    </div>
    <!-- NEU v9.1: IBS Batterie-Card (vollständig) -->
    <div class="live-card" id="card-batterie">
      <div class="live-card-title">🔋 Batterie (Aufbau · Hella IBS)</div>
      <div class="live-row"><span class="live-label">Spannung</span><span class="live-val" id="ls-batt-volt">--</span></div>
      <div class="live-row"><span class="live-label">Strom</span><span class="live-val" id="ls-batt-amp">--</span></div>
      <div class="live-row"><span class="live-label">SOC</span><span class="live-val" id="ls-batt-soc">--</span></div>
      <div class="live-row"><span class="live-label">SOH</span><span class="live-val" id="ls-batt-soh">--</span></div>
      <div class="live-row"><span class="live-label">Temperatur</span><span class="live-val" id="ls-batt-temp">--</span></div>
      <div class="live-row"><span class="live-label">Max. Kapazität</span><span class="live-val" id="ls-batt-maxcap">--</span></div>
      <div class="live-row"><span class="live-label">Verf. Kapazität</span><span class="live-val" id="ls-batt-cap">--</span></div>
      <div class="live-row"><span class="live-label">Konfiguriert</span><span class="live-val" id="ls-batt-cfgcap">--</span></div>
      <div class="live-row"><span class="live-label">Kalibriert</span><span class="live-val" id="ls-batt-cal">--</span></div>
    </div>
    <!-- NEU v9.1: Votronic Solar-Card -->
    <div class="live-card" id="card-solar">
      <div class="live-card-title">☀️ Solar (Votronic)</div>
      <div class="live-row"><span class="live-label">Ladestrom</span><span class="live-val" id="ls-solar-strom">--</span></div>
      <div class="live-row"><span class="live-label">Panelspannung</span><span class="live-val" id="ls-solar-spannung">--</span></div>
      <div class="live-row"><span class="live-label">Lademodus</span><span class="live-val" id="ls-solar-modus">--</span></div>
      <div class="live-row" style="margin-top:4px"><span class="live-label" style="color:#555;font-size:10px">⚠ Protokoll noch nicht verifiziert</span></div>
    </div>
    <div class="live-card" id="card-fahrzeug">
      <div class="live-card-title">🚗 Fahrzeug</div>
      <div class="live-row"><span class="live-label">Zündung</span><span class="live-val" id="ls-fzg-zuend">--</span></div>
      <div class="live-row"><span class="live-label">Motor</span><span class="live-val" id="ls-fzg-motor">--</span></div>
    </div>
  </div>
</div>

<!-- Steuerungspanel -->
<div class="ctrl-panel">
  <h3>🎮 Steuerung (LIN-TX)</h3>
  <div class="ctrl-warning">⚠ Achtung: Befehle werden direkt auf den LIN-Bus gesendet. Nur verwenden wenn Fahrzeug steht.</div>
  <div class="ctrl-section" style="margin-bottom:12px;border:2px solid #f0a500">
    <h4>🎛️ LIN-Master-Modus</h4>
    <div style="font-size:11px;color:#aaa;margin-bottom:8px">WomoLIN 2.1: Display bereits entfernt → Master-Modus immer aktiv.</div>
    <div class="ctrl-row">
      <button class="ctrl-btn ctrl-btn-send" onclick="setMaster(1)" id="btn-master-an">▶ Master AN</button>
      <button class="ctrl-btn ctrl-btn-off"  onclick="setMaster(0)" id="btn-master-aus">■ Master AUS</button>
    </div>
    <div class="ctrl-status" id="ctrl-master-status"></div>
  </div>
  <div class="ctrl-grid">
    <div class="ctrl-section">
      <h4>🔥 Standheizung (0x39)</h4>
      <div class="ctrl-row"><label>Modus</label>
        <select id="ctrl-heiz-modus"><option value="an">HEIZUNG AN</option><option value="umluft">UMLUFT</option><option value="aus">AUS</option></select>
      </div>
      <div class="ctrl-row"><label>Solltemp</label>
        <input type="range" id="ctrl-heiz-soll-range" min="8" max="38" value="22" oninput="document.getElementById('ctrl-heiz-soll-val').textContent=this.value">
        <span class="ctrl-val" id="ctrl-heiz-soll-val">22</span> °C
      </div>
      <div class="ctrl-row"><label>Stufe</label>
        <select id="ctrl-heiz-intensiv"><option value="0">COMFORT</option><option value="1">INTENSIV</option></select>
      </div>
      <div class="ctrl-row" style="gap:8px;margin-top:10px">
        <button class="ctrl-btn ctrl-btn-send" onclick="sendHeizung()">▶ Senden</button>
        <button class="ctrl-btn ctrl-btn-off" onclick="sendHeizungAus()">■ AUS</button>
      </div>
      <div class="ctrl-status" id="ctrl-heiz-status"></div>
    </div>
    <div class="ctrl-section">
      <h4>❄️ Kühlbox (0x01)</h4>
      <div class="ctrl-row"><label>Stufe</label>
        <select id="ctrl-kuehl-stufe"><option value="1">Stufe 1</option><option value="2">Stufe 2</option><option value="3" selected>Stufe 3</option><option value="4">Stufe 4</option><option value="5">Stufe 5</option></select>
      </div>
      <div class="ctrl-row" style="gap:8px;margin-top:10px">
        <button class="ctrl-btn ctrl-btn-send" onclick="sendKuehlbox()">▶ Senden</button>
        <button class="ctrl-btn ctrl-btn-off" onclick="sendKuehlboxAus()">■ AUS</button>
      </div>
      <div class="ctrl-status" id="ctrl-kuehl-status"></div>
    </div>
    <div class="ctrl-section">
      <h4>💧 Wasserpumpe (0x01 B0)</h4>
      <div class="ctrl-row" style="gap:8px;margin-top:10px">
        <button class="ctrl-btn ctrl-btn-send" onclick="sendPumpe(1)">▶ Pumpe AN</button>
        <button class="ctrl-btn ctrl-btn-off"  onclick="sendPumpe(0)">■ Pumpe AUS</button>
      </div>
      <div class="ctrl-status" id="ctrl-pumpe-status"></div>
    </div>
    <div class="ctrl-section">
      <h4>💡 Licht (0x05)</h4>
      <div class="ctrl-row" style="gap:8px;margin-top:10px">
        <button class="ctrl-btn ctrl-btn-send" onclick="sendLicht(1)">▶ Licht AN</button>
        <button class="ctrl-btn ctrl-btn-off"  onclick="sendLicht(0)">■ Licht AUS</button>
      </div>
      <div style="margin-top:12px">
        <label style="color:#aaa;font-size:12px">Helligkeit: <span id="licht-hell-val">5</span>/10</label>
        <input type="range" min="1" max="10" value="5" id="licht-hell-slider"
               style="width:100%;margin-top:4px;accent-color:#e94560"
               oninput="document.getElementById('licht-hell-val').textContent=this.value">
      </div>
      <div class="ctrl-status" id="ctrl-licht-status"></div>
    </div>
  </div>
</div>

<div class="controls">
  <button onclick="toggleFilter()" id="btnFilter">Nur Änderungen</button>
  <button onclick="clearLog()">Log leeren</button>
  <button onclick="downloadCSV()">📥 CSV Download</button>
  <button onclick="togglePause()" id="btnPause">⏸ Pause</button>
  <button onclick="showNvsErrors()">📋 Fehler-Log</button>
  <button onclick="showNvsUnknown()">🔍 Unbekannte</button>
  <button onclick="showNvsStats()">📊 Statistiken</button>
  <button onclick="downloadNvsJson()">💾 NVS als JSON</button>
  <button class="danger" onclick="clearNvs()">🗑 NVS löschen</button>
</div>
<div id="scanPanel"><h3>🔍 LIN-Bus Scanner</h3><div id="scanProg" style="color:#888;font-size:12px;margin-bottom:6px;">Bereit — Scanner noch nicht gestartet</div><div id="scanRes"></div></div>
<div id="nvsPanel"><h3 id="nvsPanelTitle">NVS-Daten</h3><div id="nvsContent"></div></div>
<div id="logTable">
<table><thead><tr><th>Zeit (ms)</th><th>ID</th><th>Name</th><th>Bytes (HEX)</th><th>Dekodiert</th></tr></thead>
<tbody id="logBody"></tbody></table></div>
)rawhtml");

  server.sendContent(R"rawhtml(
<script>
let filterChanged=false,paused=false,allEntries=[],lastRaumTemp='--',lastHeizIntensivJS=0x00;
function toggleFilter(){filterChanged=!filterChanged;document.getElementById('btnFilter').classList.toggle('active',filterChanged);renderTable();}
function togglePause(){paused=!paused;document.getElementById('btnPause').textContent=paused?'▶ Weiter':'⏸ Pause';}
function clearLog(){fetch('/clear').then(()=>{allEntries=[];renderTable();});}
function renderTable(){
  const tbody=document.getElementById('logBody');let rows='';
  let entries=filterChanged?allEntries.filter(e=>e.changed):allEntries;
  for(let i=entries.length-1;i>=0;i--){
    const e=entries[i];let rc=e.error?'error':e.unknown?'unknown':e.changed?'changed':'';
    let dot=e.changed?'<span style="color:#7fff7f">●</span> ':'';
    let err=e.error?'<span style="color:#ff7f7f">⚠ </span>':'';
    let unk=e.unknown?'<span style="color:#f0a500">? </span>':'';
    let dlt=(e.delta&&e.delta>0)?`<span style="color:#888;font-size:11px">(+${e.delta}ms)</span> `:'';
    rows+=`<tr class="${rc}"><td class="ts-cell">${e.ts}</td><td class="id-cell">0x${e.id}</td><td class="name-cell">${e.name}</td><td>${err}${unk}${dot}${dlt}${e.bytes}</td><td class="decoded-cell">${e.decoded}</td></tr>`;
  }
  tbody.innerHTML=rows;
}
function setVal(elId,text,cls){
  const el=document.getElementById(elId);if(!el)return;
  if(el.textContent!==text){el.textContent=text;el.className='live-val neu';setTimeout(()=>{el.className='live-val '+(cls||'');},1500);}
  else{el.className='live-val '+(cls||'');}
}
function setCard(id,active,warning){
  const el=document.getElementById(id);if(!el)return;
  el.className='live-card'+(warning?' warning':(active?' active':''));
}
function updateLiveStatus(entries){
  entries.forEach(e=>{
    const id=parseInt(e.id,16);
    const bv=(e.rawBytes||[]).map(x=>parseInt(x,16));
    switch(id){
      case 0x01:
        if(bv.length>=2){
          const stufe=(bv[1]>>4)&0x07,pumpe=(bv[0]>>1)&0x01,an=stufe>0;
          setVal('ls-kuehl-status',an?'AN':'AUS',an?'on':'off');
          setVal('ls-kuehl-stufe',an?stufe+'/5':'--');
          if(!an){setVal('ls-kuehl-komp','AUS','off');setCard('card-kuehlbox',false,false);}
          else setCard('card-kuehlbox',true,false);
          setVal('ls-wasser-pumpe',pumpe?'AN':'AUS',pumpe?'on':'off');
        }break;
      case 0x02:
        if(bv.length>=6){
          const tI=((120-bv[0])/2).toFixed(1),tA=((120-bv[1])/2).toFixed(1),tK=(25-bv[4]).toFixed(1);
          const wF=bv[5]&0x0F,wG=(bv[5]>>4)&0x0F,hahn=bv.length>=8?(bv[7]&0x01):0;
          setVal('ls-temp-innen',tI+'°C');setVal('ls-temp-aussen',tA+'°C');
          setVal('ls-kuehl-temp',tK+'°C');
          setVal('ls-wasser-hahn',hahn?'OFFEN':'ZU',hahn?'warn':'off');
          const pF=Math.round((wF/4)*100),pG=Math.round((wG/4)*100);
          document.getElementById('ls-wasser-frisch').innerHTML=wF+'/4 <span class="water-bar"><span class="water-fill water-fresh" style="width:'+pF+'%"></span></span>';
          document.getElementById('ls-wasser-grau').innerHTML=wG+'/4 <span class="water-bar"><span class="water-fill water-grey" style="width:'+pG+'%"></span></span>';
          setCard('card-wasser',wF>0,wG>=4);
        }break;
      case 0x04:
        if(bv.length>=2){
          const komp=bv[1]===0x02,zuend=bv[0]&0x01,motor=(bv[0]>>2)&0x01;
          setVal('ls-kuehl-komp',komp?'LÄUFT':'AUS',komp?'on':'off');
          setVal('ls-fzg-zuend',zuend?'AN':'AUS',zuend?'on':'off');
          setVal('ls-fzg-motor',motor?'LÄUFT':'AUS',motor?'on':'off');
          setCard('card-fahrzeug',zuend||motor,false);
        }break;
      case 0x05:case 0x3B:
        if(bv.length>=2){
          const an=bv[0]&0x01,hell=bv[1]&0x0F,ton=(bv[1]>>4)&0x0F;
          setVal('ls-licht-an',an?'AN':'AUS',an?'on':'off');
          setVal('ls-licht-hell',an?hell+'/10':'--');setVal('ls-licht-ton',an?ton+'/10':'--');
          setVal('ls-licht-src',id===0x05?'Touchscreen':'Dimmer');
          setCard('card-licht',an,false);
        }break;
      // NEU v9.1: Votronic Solar 0x20
      case 0x18:
      if (len >= 3) {
        // B0: unbekannt (evtl. Max-Kapazitaet Schaetzung)
        // B1+B2: Ladespannung (x/1000 V) — verifiziert via sevenwatt.com
        float spannung = ((uint16_t)d[1] | ((uint16_t)d[2] << 8)) / 1000.0f;
        if (spannung > 9.0f && spannung < 20.0f) {
          snprintf(buf, sizeof(buf), "Ladespannung: %.3fV | B0=0x%02X", spannung, d[0]);
        } else {
          snprintf(buf, sizeof(buf), "Ladespannung ungueltig: %.3fV", spannung);
          hasError = true;
        }
        result = buf;
      } else { hasUnknown = true; result = "Ladegeraet (zu wenig Bytes)"; }
      break;

    case 0x19:
      if (len >= 2) {
        lastChargerSeen = millis();
        float strom = d[0] / 2.0f;
        uint8_t status = d[1];
        const char* statusStr = "Unbekannt";
        // Status-Byte 0x09 noch nicht vollstaendig entschluesselt
        // Antwortet nur bei 230V Verbindung
        snprintf(buf, sizeof(buf), "Ladestrom: %.1fA | Status: 0x%02X", strom, status);
        result = buf;
      } else { hasUnknown = true; result = "Ladegeraet (zu wenig Bytes)"; }
      break;

    case 0x20:
        if(bv.length>=5){
          const strom=((bv[0]|(bv[1]<<8))/10).toFixed(1);
          const spannung=((bv[2]|(bv[3]<<8))/100).toFixed(2);
          const modi=['Kein Solar','Bulk','Absorption','Float'];
          const modus=bv[4]<4?modi[bv[4]]:'Unbekannt';
          setVal('ls-solar-strom',strom+' A',parseFloat(strom)>0?'on':'off');
          setVal('ls-solar-spannung',spannung+' V');
          setVal('ls-solar-modus',modus,parseFloat(strom)>0?'on':'');
          setCard('card-solar',parseFloat(strom)>0,false);
        }break;
      case 0x22:
        if(bv.length>=6){
          const rawI=bv[0]|(bv[1]<<8)|(bv[2]<<16);
          const amp=((rawI-2000000)/1000).toFixed(2);
          const volt=((bv[3]|(bv[4]<<8))/1000).toFixed(3);
          const temp=(bv[5]/2-40).toFixed(1);
          const ampN=parseFloat(amp);
          setVal('ls-batt-volt',volt+' V');
          setVal('ls-batt-amp',amp+' A',ampN>0?'on':ampN<-5?'warn':'');
          setVal('ls-batt-temp',temp+'°C',parseFloat(temp)>45?'warn':'');
        }break;
      case 0x25:
        if(bv.length>=2){
          const soc=(bv[0]/2).toFixed(1),soh=(bv[1]/2).toFixed(1),socN=parseFloat(soc);
          setVal('ls-batt-soc',soc+' %',socN<20?'warn':socN>80?'on':'');
          setVal('ls-batt-soh',soh+' %',parseFloat(soh)<70?'warn':'');
          setCard('card-batterie',socN>80,socN<20);
        }break;
      case 0x26:
        if(bv.length>=4){
          const maxCap=((bv[0]|(bv[1]<<8))/10).toFixed(1);
          const avaCap=((bv[2]|(bv[3]<<8))/10).toFixed(1);
          setVal('ls-batt-maxcap',maxCap+' Ah');setVal('ls-batt-cap',avaCap+' Ah');
          if(bv.length>=5)setVal('ls-batt-cfgcap',bv[4]+' Ah');
          if(bv.length>=6)setVal('ls-batt-cal',(bv[5]&0x01)?'JA':'NEIN',(bv[5]&0x01)?'on':'warn');
        }break;
      case 0x39:
        if(bv.length>=5){
          const rawT=bv[3]|(bv[4]<<8);
          const raum=((rawT>32767?rawT-65536:rawT)/10-50).toFixed(1);
          lastRaumTemp=raum+'°C';
          if(bv.length>=8)lastHeizIntensivJS=bv[7];
          setVal('ls-heiz-raum',lastRaumTemp);
          if(bv[1]!==0x00)setVal('ls-heiz-soll',(bv[2]/2-16).toFixed(1)+'°C');
        }break;
      case 0x3A:
        if(bv.length>=8){
          const auslass=(bv[2]-40).toFixed(0),soll=(bv[4]/2-16).toFixed(0);
          const statusB=bv[6],b0=bv[0],b1=bv[1];
          let statusStr='AUS',statusCls='off';
          if(statusB===0x50){statusStr='AN';statusCls='on';}
          else if(statusB===0x40){statusStr='NACHLAUF';statusCls='neu';}
          else if(statusB===0x20){statusStr='UMLUFT';statusCls='on';}
          const heizAkt=statusB!==0x00;
          setVal('ls-heiz-status',statusStr,statusCls);
          setCard('card-heizung',heizAkt,statusB===0x50);
          let modusStr='AUS';
          if(statusB===0x20)modusStr='UMLUFT';
          else if(heizAkt)modusStr=(lastHeizIntensivJS===0x80)?'INTENSIV':'COMFORT';
          setVal('ls-heiz-modus',modusStr);
          let fehlerStr='--',fehlerCls='off';
          if(b1!==0x00){
            const fm={1:'Klasse 1: Service fällig',2:'Klasse 2: Unterspannung',3:'Klasse 3: Überspannung',4:'Klasse 4: Kraftstoff/Pumpe',5:'Klasse 5: Überhitzung',6:'Klasse 6: DEFEKT',7:'Klasse 7: Temperatursensor'};
            fehlerStr=fm[b1]||('Unbekannt B1=0x'+b1.toString(16).toUpperCase());fehlerCls=b1===1?'neu':'warn';
          }else if(b0!==0x9A){fehlerStr='Unbekannt B0=0x'+b0.toString(16).toUpperCase();fehlerCls='warn';}
          setVal('ls-heiz-fehler',fehlerStr,fehlerCls);
          setVal('ls-heiz-auslass',auslass+'°C');
          setVal('ls-heiz-soll',heizAkt?soll+'°C':'--');
          const raumEl=document.getElementById('ls-temp-innen');
          const raumOk=raumEl&&raumEl.textContent&&raumEl.textContent!=='--'&&!raumEl.textContent.includes('UNGÜLTIG');
          setVal('ls-heiz-raum',raumOk?raumEl.textContent:lastRaumTemp);
        }break;
    }
  });
}
function ctrlSetStatus(elId,msg,isOk){
  const el=document.getElementById(elId);if(!el)return;
  el.textContent=msg;el.className='ctrl-status '+(isOk===true?'ok':isOk===false?'err':'');
}
async function sendHeizung(){
  const modus=document.getElementById('ctrl-heiz-modus').value;
  const soll=document.getElementById('ctrl-heiz-soll-range').value;
  const intensiv=document.getElementById('ctrl-heiz-intensiv').value;
  if(!confirm(`Heizung: ${modus.toUpperCase()} / ${soll}°C senden?`))return;
  ctrlSetStatus('ctrl-heiz-status','Sende...',null);
  try{const r=await fetch(`/cmd/heizung?modus=${modus}&soll=${soll}&intensiv=${intensiv}`);const d=await r.json();ctrlSetStatus('ctrl-heiz-status',d.ok?`✓ Gesendet: ${modus} ${soll}°C`:'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-heiz-status','✗ Verbindungsfehler',false);}
}
async function sendHeizungAus(){
  ctrlSetStatus('ctrl-heiz-status','Sende AUS...',null);
  try{const r=await fetch('/cmd/heizung?modus=aus&soll=22&intensiv=0');const d=await r.json();ctrlSetStatus('ctrl-heiz-status',d.ok?'✓ Heizung AUS':'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-heiz-status','✗ Verbindungsfehler',false);}
}
async function setMaster(an){
  ctrlSetStatus('ctrl-master-status','Sende...',null);
  try{const r=await fetch(`/cmd/master?an=${an}`);const d=await r.json();ctrlSetStatus('ctrl-master-status',d.ok?`✓ Master ${an?'AN':'AUS'}`:'✗ Fehler',d.ok);}
  catch(e){ctrlSetStatus('ctrl-master-status','✗ Verbindungsfehler',false);}
}
async function sendPumpe(an){
  ctrlSetStatus('ctrl-pumpe-status','Sende...',null);
  try{const r=await fetch(`/cmd/pumpe?an=${an}`);const d=await r.json();ctrlSetStatus('ctrl-pumpe-status',d.ok?(an?'✓ Pumpe AN':'✓ Pumpe AUS'):'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-pumpe-status','✗ Verbindungsfehler',false);}
}
async function sendLicht(an){
  const hell=document.getElementById('licht-hell-slider').value;
  ctrlSetStatus('ctrl-licht-status','Sende...',null);
  try{const r=await fetch(`/cmd/licht?an=${an}&helligkeit=${hell}`);const d=await r.json();ctrlSetStatus('ctrl-licht-status',d.ok?(an?`✓ Licht AN | Helligkeit ${hell}/10`:'✓ Licht AUS'):'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-licht-status','✗ Verbindungsfehler',false);}
}
async function sendKuehlbox(){
  const stufe=document.getElementById('ctrl-kuehl-stufe').value;
  if(!confirm(`Kühlbox Stufe ${stufe} senden?`))return;
  ctrlSetStatus('ctrl-kuehl-status','Sende...',null);
  try{const r=await fetch(`/cmd/kuehlbox?stufe=${stufe}`);const d=await r.json();ctrlSetStatus('ctrl-kuehl-status',d.ok?`✓ Stufe ${stufe}`:'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-kuehl-status','✗ Verbindungsfehler',false);}
}
async function sendKuehlboxAus(){
  ctrlSetStatus('ctrl-kuehl-status','Sende AUS...',null);
  try{const r=await fetch('/cmd/kuehlbox?stufe=0');const d=await r.json();ctrlSetStatus('ctrl-kuehl-status',d.ok?'✓ Kühlbox AUS':'✗ '+d.reason,d.ok);}
  catch(e){ctrlSetStatus('ctrl-kuehl-status','✗ Verbindungsfehler',false);}
}

async function startScan(){
  const btn=document.getElementById('scanBtn');
  btn.textContent='⏳ Scan läuft (~5s)...';btn.disabled=true;
  document.getElementById('scanPanel').style.display='block';
  document.getElementById('scanRes').innerHTML='';
  document.getElementById('scanProg').textContent='Scanne alle IDs — bitte warten...';
  try {
    const r=await fetch('/scan/start');
    const d=await r.json();
    if(d.done && d.results){
      document.getElementById('scanProg').textContent='Fertig — '+d.results.length+' IDs antworten';
      if(d.results.length===0){
        document.getElementById('scanRes').innerHTML='<div style="color:#888">Keine unbekannten IDs gefunden</div>';
      } else {
        document.getElementById('scanRes').innerHTML=d.results.map(r=>'<div class="scan-row"><span class="scan-id">'+r.id+'</span> <span class="scan-bytes">'+r.bytes+'</span><span class="scan-crc"> CRC:'+r.crc+'</span></div>').join('');
      }
    }
  } catch(e) {
    document.getElementById('scanProg').textContent='Fehler: '+e;
  }
  btn.textContent='🔍 LIN Scanner';btn.disabled=false;
}
function pollScan(){}


async function cmdRestart(){
  if(!confirm('ESP32 wirklich neu starten?'))return;
  try{await fetch('/cmd/restart');}catch(e){}
  const s=document.getElementById('busStatus');if(s){s.textContent='NEUSTART...';s.className='bus-inactive';}
}
async function wifiReset(){
  if(!confirm('WLAN und MQTT Konfiguration loeschen?\nESP32 startet als Hotspot NuggetLIN (Passwort: nugget12).\nAlle anderen Einstellungen bleiben erhalten.'))return;
  try{await fetch('/cmd/wifireset');}catch(e){}
  const s=document.getElementById('busStatus');if(s){s.textContent='WLAN RESET — Hotspot NuggetLIN oeffnet sich...';s.className='bus-inactive';}
}
async function fetchCtrlStatus(){
  try{
    const r=await fetch('/cmd/status');const d=await r.json();
    if(d.pumpeAn!==undefined)setVal('ls-wasser-pumpe',d.pumpeAn?'AN':'AUS',d.pumpeAn?'on':'off');
    const pp=document.getElementById('ctrl-pumpe-status');
    if(pp&&d.pumpeAn){pp.textContent='';}
    else if(pp&&!d.pumpeAn)pp.textContent='';
  }catch(e){}
}
setInterval(fetchCtrlStatus,10000);fetchCtrlStatus();
async function fetchData(){
  if(paused)return;
  try{
    const r=await fetch('/data');const d=await r.json();
    document.getElementById('busStatus').textContent=d.busStatus;
    document.getElementById('busStatus').className=d.busActive?'bus-active':'bus-inactive';
    document.getElementById('fps').textContent=d.fps;
    document.getElementById('total').textContent=d.total;
    document.getElementById('rssi').textContent=d.rssi;
    document.getElementById('uptime').textContent=formatUptime(d.uptime);
    document.getElementById('nvsErrCnt').textContent=d.nvsErrCount||0;
    document.getElementById('nvsUnkCnt').textContent=d.nvsUnkCount||0;
    const pt=(d.parserTimeout||0)+(d.parserOverflow||0)+(d.parserParityError||0);
    const pe=document.getElementById('parserErrCnt');
    if(pe){pe.textContent=pt;pe.style.color=pt>0?'#f0a500':'#7fff7f';}
    if(d.entries&&d.entries.length>0){updateLiveStatus(d.entries);allEntries=allEntries.concat(d.entries);if(allEntries.length>2000)allEntries=allEntries.slice(-2000);renderTable();}
  }catch(e){}
}
function formatUptime(ms){let s=Math.floor(ms/1000),m=Math.floor(s/60);s%=60;let h=Math.floor(m/60);m%=60;return`${h}h ${m}m ${s}s`;}
function downloadCSV(){
  let csv='Zeit_ms;Delta_ms;ID;Name;B0;B1;B2;B3;B4;B5;B6;B7;CRC;CRC_OK;Geaendert;Fehler;Unbekannt;Dekodiert\n';
  allEntries.forEach(e=>{let bytes=e.rawBytes||[];while(bytes.length<8)bytes.push('');csv+=`${e.ts};${e.delta||0};0x${e.id};${e.name};${bytes.join(';')};${e.crc||'--'};${e.crcOk?1:0};${e.changed?1:0};${e.error?1:0};${e.unknown?1:0};"${e.decoded}"\n`;});
  const blob=new Blob([csv],{type:'text/csv'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='nugget_lin_'+Date.now()+'.csv';a.click();
}
async function showNvsErrors(){
  document.getElementById('nvsPanelTitle').textContent='📋 Fehler (NVS Flash)';
  document.getElementById('nvsPanel').style.display='block';document.getElementById('nvsContent').innerHTML='Lade...';
  try{const r=await fetch('/nvs_errors');const d=await r.json();if(!d.entries||d.entries.length===0){document.getElementById('nvsContent').innerHTML='<p style="color:#888">Keine Fehler.</p>';return;}
  let html='<table><thead><tr><th>Zeit</th><th>ID</th><th>Bytes</th><th>Dekodiert</th></tr></thead><tbody>';
  d.entries.forEach(e=>{html+=`<tr class="error"><td class="ts-cell">${e.ts}</td><td class="id-cell">0x${e.id}</td><td>${e.bytes}</td><td class="decoded-cell">${e.decoded}</td></tr>`;});
  html+='</tbody></table>';document.getElementById('nvsContent').innerHTML=html;}catch(ex){document.getElementById('nvsContent').innerHTML='Fehler';}
}
async function showNvsUnknown(){
  document.getElementById('nvsPanelTitle').textContent='🔍 Unbekannte (NVS Flash)';
  document.getElementById('nvsPanel').style.display='block';document.getElementById('nvsContent').innerHTML='Lade...';
  try{const r=await fetch('/nvs_unknown');const d=await r.json();if(!d.entries||d.entries.length===0){document.getElementById('nvsContent').innerHTML='<p style="color:#888">Keine.</p>';return;}
  let html='<table><thead><tr><th>Zeit</th><th>ID</th><th>Bytes</th><th>Dekodiert</th></tr></thead><tbody>';
  d.entries.forEach(e=>{html+=`<tr class="unknown"><td class="ts-cell">${e.ts}</td><td class="id-cell">0x${e.id}</td><td>${e.bytes}</td><td class="decoded-cell">${e.decoded}</td></tr>`;});
  html+='</tbody></table>';document.getElementById('nvsContent').innerHTML=html;}catch(ex){document.getElementById('nvsContent').innerHTML='Fehler';}
}
async function showNvsStats(){
  document.getElementById('nvsPanelTitle').textContent='📊 Statistiken';
  document.getElementById('nvsPanel').style.display='block';document.getElementById('nvsContent').innerHTML='Lade...';
  try{const r=await fetch('/nvs_stats');const d=await r.json();
  let html='<div class="nvs-stat-grid">';
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">Kompressor-Starts</div><div class="nvs-stat-val">${d.kompressorStarts}</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">Heizung Laufzeit</div><div class="nvs-stat-val">${Math.floor(d.heizungLaufzeitSek/3600)}h ${Math.floor((d.heizungLaufzeitSek%3600)/60)}m</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">IBS Min. Spannung</div><div class="nvs-stat-val">${d.ibsMinVolt.toFixed(3)} V</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">IBS Max. Spannung</div><div class="nvs-stat-val">${d.ibsMaxVolt.toFixed(3)} V</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">IBS Min. Temp</div><div class="nvs-stat-val">${d.ibsMinTemp.toFixed(1)} °C</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">IBS Max. Temp</div><div class="nvs-stat-val">${d.ibsMaxTemp.toFixed(1)} °C</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">IBS Min. SOC</div><div class="nvs-stat-val">${d.ibsMinSoc.toFixed(1)} %</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">NVS Fehler</div><div class="nvs-stat-val">${d.nvsErrCount}</div></div>`;
  html+=`<div class="nvs-stat-item"><div class="nvs-stat-label">NVS Unbekannte</div><div class="nvs-stat-val">${d.nvsUnkCount}</div></div>`;
  html+='</div>';document.getElementById('nvsContent').innerHTML=html;}catch(ex){document.getElementById('nvsContent').innerHTML='Fehler';}
}
async function downloadNvsJson(){
  try{const[rE,rU,rS]=await Promise.all([fetch('/nvs_errors'),fetch('/nvs_unknown'),fetch('/nvs_stats')]);
  const[dE,dU,dS]=await Promise.all([rE.json(),rU.json(),rS.json()]);
  const blob=new Blob([JSON.stringify({errors:dE.entries||[],unknown:dU.entries||[],stats:dS,exportTime:new Date().toISOString()},null,2)],{type:'application/json'});
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='nugget_nvs_'+Date.now()+'.json';a.click();}
  catch(ex){alert('Fehler: '+ex);}
}
async function clearNvs(){
  if(!confirm('Alle NVS-Daten löschen?'))return;
  await fetch('/nvs_clear');document.getElementById('nvsPanel').style.display='none';
  document.getElementById('nvsErrCnt').textContent='0';document.getElementById('nvsUnkCnt').textContent='0';alert('NVS gelöscht.');
}
setInterval(fetchData,500);fetchData();
</script></body></html>)rawhtml");
  server.sendContent("");
}

// ============================================================
// WEBSERVER: JSON API
// ============================================================
uint32_t lastSentTotal = 0;

void handleData() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  char hdr[400];
  snprintf(hdr, sizeof(hdr),
    "{\"busStatus\":\"%s\",\"busActive\":%s,\"fps\":%u,\"total\":%u,\"rssi\":%d,\"uptime\":%lu,"
    "\"nvsErrCount\":%d,\"nvsUnkCount\":%d,\"parserTimeout\":%u,\"parserOverflow\":%u,\"parserParityError\":%u,\"entries\":[",
    busStatus.c_str(), busStatus == "AKTIV" ? "true" : "false",
    framesPerSec, framesTotal, WiFi.RSSI(), millis(),
    nvsErrCount, nvsUnkCount,
    parserStats.rxTimeout, parserStats.rxLengthOverflow, parserStats.rxParityError);
  server.sendContent(hdr);

  int unsent = (int)(totalLoggedCount - lastSentTotal);
  if (unsent < 0) unsent = 0;
  int newCount = min(unsent, 50);
  lastSentTotal = totalLoggedCount;

  bool first = true;
  static char entryBuf[600];
  for (int i = 0; i < newCount; i++) {
    portENTER_CRITICAL(&logMux);
    int idx = (logHead - newCount + i + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    LogEntry snap = logEntries[idx];
    portEXIT_CRITICAL(&logMux);
    char idHex[5]; snprintf(idHex, sizeof(idHex), "%02X", snap.id);
    char bytesArr[50] = "["; char bytesStr[50] = "";
    for (uint8_t b = 0; b < snap.dataLen; b++) {
      char hex[8]; snprintf(hex, sizeof(hex), b>0?",\"%02X\"":"\"%02X\"", snap.data[b]);
      strncat(bytesArr, hex, sizeof(bytesArr)-strlen(bytesArr)-1);
      char hs[5]; snprintf(hs, sizeof(hs), "%02X ", snap.data[b]);
      strncat(bytesStr, hs, sizeof(bytesStr)-strlen(bytesStr)-1);
    }
    strncat(bytesArr, "]", sizeof(bytesArr)-strlen(bytesArr)-1);
    String decodedEsc = snap.decoded; decodedEsc.replace("\"", "'");
    snprintf(entryBuf, sizeof(entryBuf),
      "%s{\"ts\":%lu,\"delta\":%lu,\"id\":\"%s\",\"name\":\"%s\",\"changed\":%s,\"error\":%s,\"unknown\":%s,\"crc\":\"%02X\",\"crcOk\":%s,\"decoded\":\"%s\",\"rawBytes\":%s,\"bytes\":\"%s\"}",
      first?"":","  , snap.timestamp, snap.deltaMs, idHex, getFrameName(snap.id),
      snap.changed?"true":"false", snap.hasError?"true":"false", snap.hasUnknown?"true":"false",
      snap.crc, snap.crcOk?"true":"false", decodedEsc.c_str(), bytesArr, bytesStr);
    server.sendContent(entryBuf);
    first = false;
  }
  server.sendContent("]}");
  server.sendContent("");
}

void handleClear() {
  portENTER_CRITICAL(&logMux);
  logHead=0;logCount=0;framesTotal=0;totalLoggedCount=0;lastSentTotal=0;
  portEXIT_CRITICAL(&logMux);
  server.send(200,"text/plain","OK");
}

void handleNvsErrors() {
  String json="{\"entries\":[";bool first=true;
  int start=(nvsErrCount<NVS_ERROR_MAX)?0:nvsErrHead;
  for(int i=0;i<nvsErrCount;i++){
    int idx=(start+i)%NVS_ERROR_MAX;char key[6];snprintf(key,sizeof(key),"e%02d",idx);
    String val=prefErr.getString(key,"");if(val.length()==0)continue;
    int c1=val.indexOf(':'),c2=val.indexOf(':',c1+1),c3=val.indexOf(':',c2+1);if(c1<0||c2<0||c3<0)continue;
    String ts=val.substring(0,c1),idStr=val.substring(c1+1,c2),bytesHx=val.substring(c2+1,c3),decoded=val.substring(c3+1);
    String bf="";for(uint16_t i2=0;i2+1<bytesHx.length();i2+=2)bf+=bytesHx.substring(i2,i2+2)+" ";
    if(!first)json+=",";first=false;
    json+="{\"ts\":\""+ts+"\",\"id\":\""+idStr+"\",\"bytes\":\""+bf+"\",\"decoded\":\""+decoded+"\"}";
  }
  json+="]}";server.send(200,"application/json",json);
}

void handleNvsUnknown() {
  String json="{\"entries\":[";bool first=true;
  for(int i=0;i<nvsUnkCount;i++){
    char key[6];snprintf(key,sizeof(key),"u%02d",i);
    String val=prefUnk.getString(key,"");if(val.length()==0)continue;
    int c1=val.indexOf(':'),c2=val.indexOf(':',c1+1),c3=val.indexOf(':',c2+1);if(c1<0||c2<0||c3<0)continue;
    String ts=val.substring(0,c1),idStr=val.substring(c1+1,c2),bytesHx=val.substring(c2+1,c3),decoded=val.substring(c3+1);
    String bf="";for(uint16_t i2=0;i2+1<bytesHx.length();i2+=2)bf+=bytesHx.substring(i2,i2+2)+" ";
    if(!first)json+=",";first=false;
    json+="{\"ts\":\""+ts+"\",\"id\":\""+idStr+"\",\"bytes\":\""+bf+"\",\"decoded\":\""+decoded+"\"}";
  }
  json+="]}";server.send(200,"application/json",json);
}

void handleNvsStats() {
  String json="{";
  json+="\"kompressorStarts\":"+String(nvsStats.kompressorStarts)+",";
  json+="\"heizungLaufzeitSek\":"+String(nvsStats.heizungLaufzeitSek)+",";
  json+="\"ibsMinVolt\":"+String(nvsStats.ibsMinVolt,3)+",";
  json+="\"ibsMaxVolt\":"+String(nvsStats.ibsMaxVolt,3)+",";
  json+="\"ibsMinTemp\":"+String(nvsStats.ibsMinTemp,1)+",";
  json+="\"ibsMaxTemp\":"+String(nvsStats.ibsMaxTemp,1)+",";
  json+="\"ibsMinSoc\":"+String(nvsStats.ibsMinSoc,1)+",";
  json+="\"nvsErrCount\":"+String(nvsErrCount)+",";
  json+="\"nvsUnkCount\":"+String(nvsUnkCount)+"}";
  server.send(200,"application/json",json);
}

void handleNvsClear() { nvsClearAll(); server.send(200,"text/plain","OK"); }

void handleStatus() {
  String json="{\"frames\":[";bool first=true;
  for(uint8_t id=0;id<=0x3F;id++){
    if(!frameStates[id].valid)continue;
    if(!first)json+=",";first=false;
    char idHex[5];snprintf(idHex,sizeof(idHex),"%02X",id);
    json+="{\"id\":\""+String(idHex)+"\",\"name\":\""+String(getFrameName(id))+"\",";
    json+="\"count\":"+String(frameStates[id].count)+",";
    json+="\"lastSeen\":"+String(millis()-frameStates[id].lastSeen)+",";
    json+="\"bytes\":\"";
    for(uint8_t b=0;b<frameStates[id].dataLen;b++){char hex[4];snprintf(hex,sizeof(hex),"%02X ",frameStates[id].data[b]);json+=hex;}
    json+="\"}"  ;
  }
  json+="]}";server.send(200,"application/json",json);
}

// ============================================================
// MQTT
// ============================================================
unsigned long lastMqttPublish   = 0;
unsigned long lastMqttReconnect = 0;
#define MQTT_PUBLISH_MS   5000UL
#define MQTT_RECONNECT_MS 15000UL

static void mqttDiscoverySensor(const char* id, const char* name, const char* unit,
                                 const char* devClass, const char* stateTopic,
                                 const char* valueTemplate, const char* icon = nullptr) {
  char topic[120], payload[600];
  snprintf(topic, sizeof(topic), "%s/sensor/%s_%s/config", MQTT_PREFIX, MQTT_DEV, id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\",\"value_template\":\"%s\","
    "%s%s%s%s%s%s%s%s%s"
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Ford Nugget LIN-Bus\","
    "\"model\":\"ESP32 WomoLIN 2.1\",\"manufacturer\":\"DIY\"}}",
    name, MQTT_DEV, id, stateTopic, valueTemplate,
    unit?     "\"unit_of_measurement\":\"":"", unit?unit:"",         unit?"\",":"",
    devClass? "\"device_class\":\"":"",        devClass?devClass:"", devClass?"\",":"",
    icon?     "\"icon\":\"":"",               icon?icon:"",          icon?"\",":"",
    MQTT_DEV);
  mqttClient.publish(topic, payload, true);
}

static void mqttDiscoverySwitch(const char* id, const char* name,
                                 const char* stateTopic, const char* cmdTopic,
                                 const char* icon = "mdi:toggle-switch") {
  char topic[120], payload[512];
  snprintf(topic, sizeof(topic), "%s/switch/%s_%s/config", MQTT_PREFIX, MQTT_DEV, id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\",\"command_topic\":\"%s\","
    "\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"icon\":\"%s\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Ford Nugget LIN-Bus\","
    "\"model\":\"ESP32 WomoLIN 2.1\",\"manufacturer\":\"DIY\"}}",
    name, MQTT_DEV, id, stateTopic, cmdTopic, icon, MQTT_DEV);
  mqttClient.publish(topic, payload, true);
}

static void mqttDiscoveryNumber(const char* id, const char* name,
                                 const char* stateTopic, const char* cmdTopic,
                                 float minV, float maxV, float step,
                                 const char* unit, const char* icon = "mdi:numeric") {
  char topic[120], payload[600];
  snprintf(topic, sizeof(topic), "%s/number/%s_%s/config", MQTT_PREFIX, MQTT_DEV, id);
  snprintf(payload, sizeof(payload),
    "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\",\"command_topic\":\"%s\","
    "\"min\":%.1f,\"max\":%.1f,\"step\":%.1f,\"unit_of_measurement\":\"%s\",\"icon\":\"%s\","
    "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Ford Nugget LIN-Bus\","
    "\"model\":\"ESP32 WomoLIN 2.1\",\"manufacturer\":\"DIY\"}}",
    name, MQTT_DEV, id, stateTopic, cmdTopic, minV, maxV, step, unit, icon, MQTT_DEV);
  mqttClient.publish(topic, payload, true);
}

void mqttSendDiscovery() {
  char st[80];
  snprintf(st, sizeof(st), "%s/%s/state", MQTT_PREFIX, MQTT_DEV);

  // ---- Temperaturen ----
  mqttDiscoverySensor("t_innen",    "Innentemperatur",      "°C", "temperature", st, "{{value_json.t_innen}}",    "mdi:home-thermometer");
  mqttDiscoverySensor("t_aussen",   "Außentemperatur",      "°C", "temperature", st, "{{value_json.t_aussen}}",   "mdi:thermometer-outside");
  mqttDiscoverySensor("t_kuehlbox", "Kühlbox Temperatur",   "°C", "temperature", st, "{{value_json.t_kuehlbox}}", "mdi:thermometer-minus");
  mqttDiscoverySensor("t_auslass",  "Standheizung Auslass", "°C", "temperature", st, "{{value_json.t_auslass}}",  "mdi:thermometer-chevron-up");

  // ---- Wasser ----
  mqttDiscoverySensor("wasser_frisch", "Frischwasser", "%",    nullptr, st, "{{value_json.wasser_frisch}}", "mdi:water");
  mqttDiscoverySensor("wasser_grau",   "Grauwasser",   "%",    nullptr, st, "{{value_json.wasser_grau}}",   "mdi:water-off");
  mqttDiscoverySensor("wasser_hahn",   "Wasserhahn",   nullptr,nullptr, st, "{{value_json.wasser_hahn}}",   "mdi:faucet");

  // ---- Fahrzeug ----
  mqttDiscoverySensor("zuendung", "Zündung", nullptr, nullptr, st, "{{value_json.zuendung}}", "mdi:key-variant");
  mqttDiscoverySensor("motor",    "Motor",   nullptr, nullptr, st, "{{value_json.motor}}",    "mdi:engine");

  // ---- Standheizung & Kühlbox ----
  mqttDiscoverySensor("heiz_status", "Standheizung Status", nullptr, nullptr, st, "{{value_json.heiz_status}}", "mdi:radiator");
  mqttDiscoverySensor("heiz_fehler", "Standheizung Fehler", nullptr, nullptr, st, "{{value_json.heiz_fehler}}", "mdi:alert-circle");
  mqttDiscoverySensor("kuehl_komp",  "Kompressor",          nullptr, nullptr, st, "{{value_json.kuehl_komp}}",  "mdi:air-conditioner");
  mqttDiscoverySensor("kuehl_stufe", "Kühlbox Stufe",       nullptr, nullptr, st, "{{value_json.kuehl_stufe}}", "mdi:snowflake-thermometer");
  mqttDiscoverySensor("licht_hell",  "Licht Helligkeit",    nullptr, nullptr, st, "{{value_json.licht_hell}}",  "mdi:brightness-5");

  // ---- NEU v9.1: IBS Batterie-Sensoren (in v8.3 entfernt — jetzt zurück) ----
  mqttDiscoverySensor("batt_volt",   "Batterie Spannung",         "V",  "voltage",     st, "{{value_json.batt_volt}}",   "mdi:lightning-bolt");
  mqttDiscoverySensor("batt_amp",    "Batterie Strom",            "A",  "current",     st, "{{value_json.batt_amp}}",    "mdi:current-dc");
  mqttDiscoverySensor("batt_soc",    "Batterie SOC",              "%",  "battery",     st, "{{value_json.batt_soc}}",    "mdi:battery");
  mqttDiscoverySensor("batt_soh",    "Batterie SOH",              "%",  nullptr,       st, "{{value_json.batt_soh}}",    "mdi:battery-heart");
  mqttDiscoverySensor("batt_temp",   "Batterie Temperatur",       "°C", "temperature", st, "{{value_json.batt_temp}}",   "mdi:thermometer");
  mqttDiscoverySensor("batt_cap",    "Batterie Verfügb. Kapazität","Ah", nullptr,      st, "{{value_json.batt_cap}}",    "mdi:battery-charging");
  mqttDiscoverySensor("batt_maxcap", "Batterie Max. Kapazität",   "Ah", nullptr,       st, "{{value_json.batt_maxcap}}", "mdi:battery-plus");
  mqttDiscoverySensor("batt_cfgcap", "Batterie Konfiguriert",     "Ah", nullptr,       st, "{{value_json.batt_cfgcap}}", "mdi:battery-settings");
  mqttDiscoverySensor("batt_cal",    "Batterie Kalibriert",       nullptr, nullptr,    st, "{{value_json.batt_cal}}",    "mdi:check-circle");

  // ---- NEU v9.1: Votronic Solar-Sensoren ----
  mqttDiscoverySensor("solar_strom",   "Solar Ladestrom",   "A",  "current", st, "{{value_json.solar_strom}}",   "mdi:solar-power");
  mqttDiscoverySensor("solar_spannung","Solar Panelspannung","V",  "voltage", st, "{{value_json.solar_spannung}}", "mdi:solar-panel");
  mqttDiscoverySensor("solar_modus",   "Solar Lademodus",   nullptr,nullptr,  st, "{{value_json.solar_modus}}",   "mdi:battery-charging-100");

  // ---- Ladegeraet ----
  mqttDiscoverySensor("charger_status",  "Ladegeraet Status",   nullptr, nullptr,  st, "{{value_json.charger_status}}",  "mdi:ev-station");
  mqttDiscoverySensor("charger_strom",   "Ladegeraet Strom",    "A",    "current", st, "{{value_json.charger_strom}}",   "mdi:current-ac");
  mqttDiscoverySensor("charger_spannung","Ladegeraet Spannung", "V",    "voltage", st, "{{value_json.charger_spannung}}","mdi:lightning-bolt");

  // ---- Schalter ----
  char cst[80], cmd[80];
  snprintf(cst, sizeof(cst), "%s/%s/kuehlbox/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/kuehlbox/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoverySwitch("kuehlbox", "Kühlbox", cst, cmd, "mdi:fridge-outline");

  snprintf(cst, sizeof(cst), "%s/%s/heizung/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/heizung/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoverySwitch("heizung", "Standheizung", cst, cmd, "mdi:car-seat-heater");

  snprintf(cst, sizeof(cst), "%s/%s/pumpe/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/pumpe/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoverySwitch("pumpe", "Wasserpumpe", cst, cmd, "mdi:water-pump");

  snprintf(cst, sizeof(cst), "%s/%s/licht/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/licht/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoverySwitch("licht", "Innenbeleuchtung", cst, cmd, "mdi:ceiling-light");

  // ---- Regler ----
  snprintf(cst, sizeof(cst), "%s/%s/heiz_soll/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/heiz_soll/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoveryNumber("heiz_soll", "Standheizung Solltemperatur", cst, cmd, 15, 28, 1, "°C", "mdi:thermometer-lines");

  snprintf(cst, sizeof(cst), "%s/%s/kuehl_stufe/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/kuehl_stufe/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoveryNumber("kuehl_stufe_ctrl", "Kühlbox Stufe", cst, cmd, 1, 5, 1, "", "mdi:snowflake-thermometer");

  snprintf(cst, sizeof(cst), "%s/%s/licht_hell/state", MQTT_PREFIX, MQTT_DEV);
  snprintf(cmd, sizeof(cmd),  "%s/%s/licht_hell/set",  MQTT_PREFIX, MQTT_DEV);
  mqttDiscoveryNumber("licht_hell_ctrl", "Licht Helligkeit", cst, cmd, 1, 10, 1, "", "mdi:brightness-5");

  Serial.println("[MQTT] Discovery gesendet (inkl. IBS Batterie + Votronic Solar)");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[32]; memset(msg, 0, sizeof(msg));
  memcpy(msg, payload, min((unsigned int)(sizeof(msg)-1), length));
  char topicStr[120]; strncpy(topicStr, topic, sizeof(topicStr)-1);

  char heizSet[80], kuehlSwSet[80], pumpeSet[80], lichtSet[80], heizSollSet[80], kuehlSet[80], lichtHellSet[80];
  snprintf(heizSet,     sizeof(heizSet),     "%s/%s/heizung/set",     MQTT_PREFIX, MQTT_DEV);
  snprintf(kuehlSwSet,  sizeof(kuehlSwSet),  "%s/%s/kuehlbox/set",    MQTT_PREFIX, MQTT_DEV);
  snprintf(pumpeSet,    sizeof(pumpeSet),    "%s/%s/pumpe/set",       MQTT_PREFIX, MQTT_DEV);
  snprintf(lichtSet,    sizeof(lichtSet),    "%s/%s/licht/set",       MQTT_PREFIX, MQTT_DEV);
  snprintf(heizSollSet, sizeof(heizSollSet), "%s/%s/heiz_soll/set",   MQTT_PREFIX, MQTT_DEV);
  snprintf(kuehlSet,    sizeof(kuehlSet),    "%s/%s/kuehl_stufe/set", MQTT_PREFIX, MQTT_DEV);
  snprintf(lichtHellSet,sizeof(lichtHellSet),"%s/%s/licht_hell/set",  MQTT_PREFIX, MQTT_DEV);

  if (strcmp(topicStr, heizSet) == 0) {
    bool an = strcmp(msg, "ON") == 0;
    int16_t raumRaw = 700;
    if (frameStates[0x02].valid)
      raumRaw = (int16_t)(((120.0f - frameStates[0x02].data[0]) / 2.0f - 2.0f + 50.0f) * 10.0f);  // -2.0°C Offset
    uint8_t soll = ctrl.heizSoll > 0 ? ctrl.heizSoll : (uint8_t)((22+16)*2);
    linSendHeizung(an ? 0x02 : 0x00, soll, ctrl.heizIntensiv, raumRaw);
  } else if (strcmp(topicStr, kuehlSwSet) == 0) {
    bool an = strcmp(msg, "ON") == 0;
    linSendKuehlbox(an ? (ctrl.kuehlLastStufe > 0 ? ctrl.kuehlLastStufe : 3) : 0);
  } else if (strcmp(topicStr, pumpeSet) == 0) {
    ctrl.pumpeAn = strcmp(msg, "ON") == 0;
    linSendDisplay(false);
  } else if (strcmp(topicStr, lichtSet) == 0) {
    linSendLicht(strcmp(msg, "ON") == 0, ctrl.lichtHell ? ctrl.lichtHell : 5);
  } else if (strcmp(topicStr, heizSollSet) == 0) {
    int soll = atoi(msg);
    if (soll >= 15 && soll <= 28) ctrl.heizSoll = (uint8_t)((soll + 16) * 2);
  } else if (strcmp(topicStr, kuehlSet) == 0) {
    int stufe = atoi(msg);
    if (stufe >= 1 && stufe <= 5) linSendKuehlbox((uint8_t)stufe);
  } else if (strcmp(topicStr, lichtHellSet) == 0) {
    int hell = atoi(msg);
    if (hell >= 1 && hell <= 10) linSendLicht(ctrl.lichtAn, (uint8_t)hell);
  }
}

void mqttSubscribeAll() {
  char cmd[80];
  snprintf(cmd, sizeof(cmd), "%s/%s/heizung/set",     MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/kuehlbox/set",    MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/pumpe/set",       MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/licht/set",       MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/heiz_soll/set",   MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/kuehl_stufe/set", MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
  snprintf(cmd, sizeof(cmd), "%s/%s/licht_hell/set",  MQTT_PREFIX, MQTT_DEV); mqttClient.subscribe(cmd);
}

bool mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (strlen(cfg_mqtt_broker) == 0) { Serial.println("[MQTT] Kein Broker konfiguriert"); return false; }
  int port = atoi(cfg_mqtt_port);
  if (port <= 0) port = 1883;
  mqttClient.setServer(cfg_mqtt_broker, port);
  mqttClient.setBufferSize(1024);
  char will[80]; snprintf(will, sizeof(will), "%s/%s/status", MQTT_PREFIX, MQTT_DEV);
  const char* user = strlen(cfg_mqtt_user) > 0 ? cfg_mqtt_user : nullptr;
  const char* pass = strlen(cfg_mqtt_pass) > 0 ? cfg_mqtt_pass : nullptr;
  if (mqttClient.connect(MQTT_CLIENT, user, pass, will, 1, true, "offline")) {
    Serial.printf("[MQTT] Verbunden mit %s:%d\n", cfg_mqtt_broker, port);
    mqttClient.publish(will, "online", true);
    mqttSendDiscovery();
    mqttSubscribeAll();
    return true;
  }
  Serial.printf("[MQTT] Fehler: %d (Broker: %s:%d)\n", mqttClient.state(), cfg_mqtt_broker, port);
  return false;
}

void mqttPublishState() {
  if (!mqttClient.connected()) return;
  char topic[80], payload[1100];

  snprintf(topic, sizeof(topic), "%s/%s/state", MQTT_PREFIX, MQTT_DEV);

  // Alle Werte aus frameStates lesen
  float tInnen=-99,tAussen=-99,tKuehl=-99,tAuslass=-99;
  float battVolt=-1,battAmp=-999,battSoc=-1,battSoh=-1,battTemp=-99,battCap=-1,battMaxCap=-1;
  int   battCfgCap=-1; bool battCal=false;
  float solarStrom=-1,solarSpannung=-1; const char* solarModus="--";
  int wasserFrisch=-1,wasserGrau=-1;
  const char* wasserHahn="--",*zuendung="AUS",*motor="AUS";
  const char* heizStatus="AUS",*heizFehler="--",*kuehlKomp="AUS";
  const char* chargerStatus="Nicht verbunden";
  float chargerStrom=-1, chargerSpannung=-1;

  if (frameStates[0x02].valid && frameStates[0x02].dataLen >= 8) {
    uint8_t* d = frameStates[0x02].data;
    tInnen=( 120.0f-d[0])/2.0f; tAussen=(120.0f-d[1])/2.0f; tKuehl=25.0f-d[4];
    wasserFrisch=(d[5]&0x0F)*25; wasserGrau=((d[5]>>4)&0x0F)*25;
    wasserHahn=(d[7]&0x01)?"OFFEN":"ZU";
  }
  if (frameStates[0x04].valid && frameStates[0x04].dataLen >= 2) {
    zuendung=(frameStates[0x04].data[0]&0x01)?"AN":"AUS";
    motor=(frameStates[0x04].data[0]&0x04)?"AN":"AUS";
    kuehlKomp=(frameStates[0x04].data[1]==0x02)?"LÄUFT":"AUS";
  }
  if (frameStates[0x22].valid && frameStates[0x22].dataLen >= 6) {
    uint8_t* d=frameStates[0x22].data;
    uint32_t raw=(uint32_t)d[0]|((uint32_t)d[1]<<8)|((uint32_t)d[2]<<16);
    battAmp=((float)raw-2000000.0f)/1000.0f;
    battVolt=((uint16_t)d[3]|((uint16_t)d[4]<<8))/1000.0f;
    battTemp=d[5]/2.0f-40.0f;
  }
  if (frameStates[0x25].valid && frameStates[0x25].dataLen >= 2) {
    battSoc=frameStates[0x25].data[0]/2.0f;
    battSoh=frameStates[0x25].data[1]/2.0f;
  }
  if (frameStates[0x26].valid && frameStates[0x26].dataLen >= 4) {
    uint8_t* d=frameStates[0x26].data;
    battMaxCap=((uint16_t)d[0]|((uint16_t)d[1]<<8))/10.0f;
    battCap   =((uint16_t)d[2]|((uint16_t)d[3]<<8))/10.0f;
    if (frameStates[0x26].dataLen >= 5) battCfgCap = d[4];
    if (frameStates[0x26].dataLen >= 6) battCal = (d[5] & 0x01);
  }
  // NEU v9.1: Votronic Solar
  if (frameStates[0x20].valid && frameStates[0x20].dataLen >= 5) {
    uint8_t* d=frameStates[0x20].data;
    solarStrom   =((uint16_t)d[0]|((uint16_t)d[1]<<8))/10.0f;
    solarSpannung=((uint16_t)d[2]|((uint16_t)d[3]<<8))/100.0f;
    switch(d[4]){case 0:solarModus="Kein Solar";break;case 1:solarModus="Bulk";break;case 2:solarModus="Absorption";break;case 3:solarModus="Float";break;default:solarModus="Unbekannt";}
  }
  if (frameStates[0x3A].valid && frameStates[0x3A].dataLen >= 8) {
    uint8_t* d=frameStates[0x3A].data;
    tAuslass=d[2]-40.0f;
    uint8_t b6=d[6];
    if(b6==0x50)heizStatus="AN";else if(b6==0x40)heizStatus="NACHLAUF";else if(b6==0x20)heizStatus="UMLUFT";else heizStatus="AUS";
    if(d[1]==0x01)heizFehler="Service fällig";else if(d[1]>=0x02)heizFehler="Fehler";else heizFehler="--";
  }

  // Ladegeraet Status
  if (lastChargerSeen > 0 && (millis() - lastChargerSeen) < CHARGER_TIMEOUT_MS) {
    chargerStatus = "Verbunden";
    if (frameStates[0x19].valid && frameStates[0x19].dataLen >= 2)
      chargerStrom = frameStates[0x19].data[0] / 2.0f;
    if (frameStates[0x18].valid && frameStates[0x18].dataLen >= 3)
      chargerSpannung = ((uint16_t)frameStates[0x18].data[1] | ((uint16_t)frameStates[0x18].data[2] << 8)) / 1000.0f;
  }

  // Haupt-State JSON mit allen Werten
  snprintf(payload, sizeof(payload),
    "{\"t_innen\":%.1f,\"t_aussen\":%.1f,\"t_kuehlbox\":%.1f,\"t_auslass\":%.1f,"
    "\"batt_volt\":%.3f,\"batt_amp\":%.2f,\"batt_soc\":%.1f,\"batt_soh\":%.1f,"
    "\"batt_temp\":%.1f,\"batt_cap\":%.1f,\"batt_maxcap\":%.1f,\"batt_cfgcap\":%d,\"batt_cal\":\"%s\","
    "\"solar_strom\":%.1f,\"solar_spannung\":%.2f,\"solar_modus\":\"%s\","
    "\"wasser_frisch\":%d,\"wasser_grau\":%d,\"wasser_hahn\":\"%s\","
    "\"zuendung\":\"%s\",\"motor\":\"%s\","
    "\"heiz_status\":\"%s\",\"heiz_fehler\":\"%s\","
    "\"kuehl_komp\":\"%s\",\"kuehl_stufe\":%d,\"licht_hell\":%d,"
    "\"charger_status\":\"%s\",\"charger_strom\":%.1f,\"charger_spannung\":%.3f}",
    battVolt,battAmp,battSoc,battSoh,battTemp,battCap,battMaxCap,battCfgCap,battCal?"JA":"NEIN",
    solarStrom,solarSpannung,solarModus,
    wasserFrisch,wasserGrau,wasserHahn,
    zuendung,motor,heizStatus,heizFehler,
    kuehlKomp,ctrl.kuehlStufe,ctrl.lichtHell,
    chargerStatus,chargerStrom,chargerSpannung);
  mqttClient.publish(topic, payload);

  // Switch States
  snprintf(topic,sizeof(topic),"%s/%s/heizung/state",MQTT_PREFIX,MQTT_DEV);
  mqttClient.publish(topic,ctrl.heizModus!=0x00?"ON":"OFF");
  snprintf(topic,sizeof(topic),"%s/%s/pumpe/state",MQTT_PREFIX,MQTT_DEV);
  mqttClient.publish(topic,ctrl.pumpeAn?"ON":"OFF");
  snprintf(topic,sizeof(topic),"%s/%s/licht/state",MQTT_PREFIX,MQTT_DEV);
  mqttClient.publish(topic,ctrl.lichtAn?"ON":"OFF");
  snprintf(topic,sizeof(topic),"%s/%s/kuehlbox/state",MQTT_PREFIX,MQTT_DEV);
  mqttClient.publish(topic,ctrl.kuehlStufe>0?"ON":"OFF");

  // Number States
  float heizSollDeg=ctrl.heizSoll/2.0f-16.0f;
  snprintf(topic,sizeof(topic),"%s/%s/heiz_soll/state",MQTT_PREFIX,MQTT_DEV);
  snprintf(payload,16,"%.1f",heizSollDeg);mqttClient.publish(topic,payload);
  snprintf(topic,sizeof(topic),"%s/%s/kuehl_stufe/state",MQTT_PREFIX,MQTT_DEV);
  snprintf(payload,8,"%d",ctrl.kuehlStufe);mqttClient.publish(topic,payload);
  snprintf(topic,sizeof(topic),"%s/%s/licht_hell/state",MQTT_PREFIX,MQTT_DEV);
  snprintf(payload,8,"%d",ctrl.lichtHell?ctrl.lichtHell:5);mqttClient.publish(topic,payload);
}

// ============================================================
// SCANNER — blockierend, laeuft komplett in handleScanStart
// ============================================================

void mqttTick() {
  unsigned long now=millis();
  if (!mqttClient.connected()) {
    if (now-lastMqttReconnect>=MQTT_RECONNECT_MS) { lastMqttReconnect=now; mqttConnect(); }
    return;
  }
  mqttClient.loop();
  if (now-lastMqttPublish>=MQTT_PUBLISH_MS) { lastMqttPublish=now; mqttPublishState(); }
}

// ============================================================
// WLAN-RESET: NVS-Konfiguration löschen und neu starten
// ============================================================
void wifiReset() {
  Serial.println("[WiFi] Konfiguration wird gelöscht...");
  prefCfg.begin("nugcfg", false);
  prefCfg.clear();
  prefCfg.end();
  wifiManager.resetSettings();
  delay(500);
  ESP.restart();
}

// ============================================================
// MQTT-KONFIGURATION aus NVS laden
// ============================================================
void loadMqttConfig() {
  prefCfg.begin("nugcfg", true);
  prefCfg.getString("mqtt_broker", cfg_mqtt_broker, sizeof(cfg_mqtt_broker));
  prefCfg.getString("mqtt_port",   cfg_mqtt_port,   sizeof(cfg_mqtt_port));
  prefCfg.getString("mqtt_user",   cfg_mqtt_user,   sizeof(cfg_mqtt_user));
  prefCfg.getString("mqtt_pass",   cfg_mqtt_pass,   sizeof(cfg_mqtt_pass));
  prefCfg.end();
  if (strlen(cfg_mqtt_port) == 0) strlcpy(cfg_mqtt_port, "1883", sizeof(cfg_mqtt_port));
  Serial.printf("[CFG] MQTT Broker: %s:%s User: %s\n",
    cfg_mqtt_broker, cfg_mqtt_port,
    strlen(cfg_mqtt_user)>0 ? cfg_mqtt_user : "(kein)");
}

// ============================================================
// MQTT-KONFIGURATION in NVS speichern
// ============================================================
void saveMqttConfig() {
  prefCfg.begin("nugcfg", false);
  prefCfg.putString("mqtt_broker", cfg_mqtt_broker);
  prefCfg.putString("mqtt_port",   cfg_mqtt_port);
  prefCfg.putString("mqtt_user",   cfg_mqtt_user);
  prefCfg.putString("mqtt_pass",   cfg_mqtt_pass);
  prefCfg.end();
  Serial.println("[CFG] MQTT-Konfiguration gespeichert.");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Nugget LIN-Bus Sniffer v9.9 ===");
  Serial.println("=== WomoLIN Interface 2.1, RX=16, TX=17, kein INH-Pin ===");
  Serial.println("=== WiFiManager aktiv: Hotspot=NuggetLIN / nugget12 ===");
  Serial.println("=== FST T151 / TJA1020: INH/SLP auf GPIO4 ===");

  memset(frameStates, 0, sizeof(frameStates));
  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.kuehlLastStufe = 3;
  ctrl.heizSoll = (uint8_t)((22 + 16) * 2);  // 22 Grad Startwert
  ctrl.heizSoll = (uint8_t)((22 + 16) * 2);  // 22°C als Startwert

  nvsLoadIndices();
  Serial.printf("[NVS] Fehler: %d/%d | Unbekannte: %d/%d\n", nvsErrCount, NVS_ERROR_MAX, nvsUnkCount, NVS_UNKNOWN_MAX);

  // INH/SLP-Pin: Transceiver aktivieren (FST T151 / TJA1020)
  pinMode(LIN_INH_PIN, OUTPUT);
  digitalWrite(LIN_INH_PIN, HIGH);
  delay(10);  // Transceiver kurz aufwachen lassen
  Serial.println("[LIN] INH/SLP GPIO4 HIGH — Transceiver aktiv");

  // Serial2: UART2 für LIN-Bus
  Serial2.begin(LIN_BAUD, SERIAL_8N1, LIN_RX_PIN, LIN_TX_PIN);

  // ---- WiFiManager konfigurieren ----
  // Custom Parameter: MQTT-Felder im Captive Portal
  WiFiManagerParameter p_broker("broker", "MQTT Broker IP",  cfg_mqtt_broker, 40);
  WiFiManagerParameter p_port  ("port",   "MQTT Port",       cfg_mqtt_port,    6);
  WiFiManagerParameter p_user  ("user",   "MQTT Benutzername", cfg_mqtt_user,  32);
  WiFiManagerParameter p_pass  ("pass",   "MQTT Passwort",   cfg_mqtt_pass,   32);

  wifiManager.addParameter(&p_broker);
  wifiManager.addParameter(&p_port);
  wifiManager.addParameter(&p_user);
  wifiManager.addParameter(&p_pass);

  wifiManager.setConfigPortalTimeout(180);   // Hotspot bleibt 3 Min offen
  wifiManager.setConnectTimeout(30);          // WLAN-Verbindung max. 30s
  wifiManager.setTitle("NuggetLIN Konfiguration");
  wifiManager.setSaveConfigCallback([&]() {
    // Wird nach erfolgreicher WLAN-Eingabe aufgerufen
    strlcpy(cfg_mqtt_broker, p_broker.getValue(), sizeof(cfg_mqtt_broker));
    strlcpy(cfg_mqtt_port,   p_port.getValue(),   sizeof(cfg_mqtt_port));
    strlcpy(cfg_mqtt_user,   p_user.getValue(),   sizeof(cfg_mqtt_user));
    strlcpy(cfg_mqtt_pass,   p_pass.getValue(),   sizeof(cfg_mqtt_pass));
    saveMqttConfig();
  });

  // MQTT-Werte aus NVS laden (Vorausfüllen der Custom Fields)
  loadMqttConfig();

  Serial.println("[WiFi] Verbinde mit gespeichertem WLAN oder öffne Hotspot NuggetLIN...");
  bool connected = wifiManager.autoConnect(WIFIMGR_SSID, WIFIMGR_PASS);

  if (connected) {
    Serial.println("[WiFi] Verbunden: " + WiFi.localIP().toString());
    if (MDNS.begin("nugget")) Serial.println("[mDNS] http://nugget.local");
    mqttClient.setCallback(mqttCallback);
    mqttConnect();
  } else {
    Serial.println("[WiFi] Kein WLAN — nur LIN-Bus-Empfang aktiv");
    wifiConfigMode = true;
  }

  // Web-UI Routen
  server.on("/",              handleRoot);
  server.on("/data",          handleData);
  server.on("/clear",         handleClear);
  server.on("/status",        handleStatus);
  server.on("/nvs_errors",    handleNvsErrors);
  server.on("/nvs_unknown",   handleNvsUnknown);
  server.on("/nvs_stats",     handleNvsStats);
  server.on("/nvs_clear",     handleNvsClear);
  server.on("/cmd/heizung",   handleCmdHeizung);
  server.on("/cmd/kuehlbox",  handleCmdKuehlbox);
  server.on("/cmd/pumpe",     handleCmdPumpe);
  server.on("/cmd/licht",     handleCmdLicht);
  server.on("/cmd/master",    handleCmdMaster);
  server.on("/cmd/status",    handleCmdStatus);
  server.on("/serial",        handleSerial);
  server.on("/cmd/restart",   handleCmdRestart);
  server.on("/cmd/wifireset", HTTP_GET, [](){ wifiReset(); });
  server.on("/scan/start",  HTTP_GET, handleScanStart);
  server.on("/scan/status", HTTP_GET, handleScanStatus);
  server.on("/update", HTTP_GET,  handleUpdateGet);
  server.on("/update", HTTP_POST, handleUpdatePost, handleUpdateUpload);
  server.begin();
  Serial.println("Sniffer v9.9 bereit — WiFiManager + Votronic Solar + IBS Batterie aktiv.");

  // OTA Rollback
  esp_ota_img_states_t otaState;
  const esp_partition_t* running=esp_ota_get_running_partition();
  if (esp_ota_get_state_partition(running,&otaState)==ESP_OK) {
    if (otaState==ESP_OTA_IMG_PENDING_VERIFY) {
      if (WiFi.status()==WL_CONNECTED) { esp_ota_mark_app_valid_cancel_rollback(); Serial.println("[OTA] Firmware gültig."); }
      else { esp_ota_mark_app_invalid_rollback_and_reboot(); }
    }
  }
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  server.handleClient();

  while (Serial2.available()) processLinByte(Serial2.read());

  if (rxState==COLLECT_DATA && rxLen>0 && (millis()-lastByteTime)>FRAME_TIMEOUT_MS) {
    if (rxLen>1) parserStats.rxTimeout++;
    if (currentID!=0xFF && rxLen>1) {
      uint8_t pid=linPID(currentID);
      uint8_t crcRx=rxBuf[rxLen-1];
      uint8_t crcCalc=linChecksumAuto(currentID,pid,rxBuf,rxLen-1);
      processCompleteFrame(currentID,rxBuf,rxLen-1,crcRx,(crcRx==crcCalc));
    }
    rxState=WAIT_BREAK;rxLen=0;currentID=0xFF;expectedLen=8;
  }

  unsigned long busTimeout = masterModeActive ? 10000 : 2000;
  busStatus = (millis()-lastByteTime < busTimeout) ? "AKTIV" : "KEIN SIGNAL";

  if (masterModeActive) {
    linMasterTick();
    heizungRepeatTx();
  } else {
    kuehlboxRepeatTx();
    heizungRepeatTx();
  }

  mqttTick();

  if (millis()-lastStatTime>=1000) {
    framesPerSec=frameCounter;frameCounter=0;lastStatTime=millis();
    ctrlWatchdog();
    if (millis()-lastParserStatPrint>=60000) {
      lastParserStatPrint=millis();
      if (parserStats.rxTimeout||parserStats.rxLengthOverflow||parserStats.rxParityError)
        Serial.printf("[PARSER] Timeout:%u Overflow:%u ParityError:%u\n",
          parserStats.rxTimeout,parserStats.rxLengthOverflow,parserStats.rxParityError);
    }
    if (lastHeizStatus==0x50 && heizStartMs>0) {
      nvsStats.heizungLaufzeitSek++;
      if (nvsStats.heizungLaufzeitSek%60==0) nvsSaveStats();
    }
  }

  static uint32_t lastSerialTotal=0;
  if (framesTotal!=lastSerialTotal) {
    lastSerialTotal=framesTotal;
    if (logCount>0) {
      int idx=(logHead-1+MAX_LOG_ENTRIES)%MAX_LOG_ENTRIES;
      LogEntry& e=logEntries[idx];
      if (e.changed) {
        char line[200];
        snprintf(line,sizeof(line),"[%8lu] ID=0x%02X %-14s: ",e.timestamp,e.id,getFrameName(e.id));
        Serial.print(line);
        for(uint8_t b=0;b<e.dataLen;b++){snprintf(line,5,"%02X ",e.data[b]);Serial.print(line);}
        if(e.hasError)Serial.print(" ⚠ FEHLER");
        Serial.print(" → ");Serial.println(e.decoded);
      }
    }
  }
}
