// Gateway OpenTherm con API HTTP (per il driver SmartThings Edge in driver/)
//
//   termostato originale <-> [slave shield] C3 mini [master shield] <-> caldaia
//
// Il gateway inoltra ogni messaggio in modo trasparente. In piu':
//  - legge tutti i dati che passano ed espone lo stato in JSON;
//  - puo' spegnere riscaldamento / acqua sanitaria (azzera i bit nel messaggio di stato);
//  - puo' limitare la temperatura di mandata richiesta dal termostato;
//  - puo' forzare il setpoint dell'acqua sanitaria;
//  - nella pausa tra i cicli del termostato chiede alla caldaia dati che il termostato non chiede.
//
// L'inoltro OpenTherm funziona anche se il Wi-Fi non e' collegato.
//
// API:
//   GET  /api/state      stato completo
//   POST /api/settings   JSON con uno o piu' di: chEnable, dhwEnable, chMax, dhwSetpoint
//   GET  /api/ids        ID OpenTherm visti (debug)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <OpenTherm.h>
#include <math.h>
#include "config.h"
#include "secrets.h"

// ===========================================================================
// OpenTherm
// ===========================================================================
OpenTherm mOT(PIN_M_IN, PIN_M_OUT);         // parla con la caldaia (fa da termostato)
OpenTherm sOT(PIN_S_IN, PIN_S_OUT, true);   // parla con il termostato (fa da caldaia)

void IRAM_ATTR mIsr() { mOT.handleInterrupt(); }
void IRAM_ATTR sIsr() { sOT.handleInterrupt(); }

// tipi di messaggio (bit 28-30)
constexpr uint8_t T_READ_DATA = 0, T_WRITE_DATA = 1, T_READ_ACK = 4, T_WRITE_ACK = 5, T_UNKNOWN_ID = 7;

// ===========================================================================
// Impostazioni modificabili da SmartThings (salvate in flash)
// ===========================================================================
struct Settings {
  bool  chEnable    = true;              // riscaldamento consentito
  bool  dhwEnable   = true;              // acqua sanitaria consentita
  float chMax       = CH_MAX_NO_LIMIT;   // limite temperatura mandata (CH_MAX_NO_LIMIT = nessun limite)
  bool  dhwOverride = false;             // setpoint sanitaria forzato da SmartThings
  float dhwSetpoint = 50;                // valore forzato (o ultimo noto)
  bool  roomControl = false;             // true = il gateway regola l'ambiente sull'obiettivo di SmartThings
  float roomTarget  = 20;                // temperatura ambiente desiderata
  bool  flowAuto    = false;             // mandata automatica (eco) mentre comanda il termostato originale
  bool  outdoorEnabled = false;          // sonda esterna collegata (attivala solo dopo aver montato la sonda)
} cfg;

Preferences prefs;
bool dhwWriteNow = false;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// stato del regolatore ambiente (aggiornato da controlStep)
bool  ctrlActive = false;   // true = sta comandando davvero (modalita' attiva e temperatura ambiente fresca)
bool  ctrlHeat = false;     // richiesta di riscaldamento calcolata
bool  ctrlFlowActive = false;   // mandata automatica in funzione (il termostato originale decide quando scaldare)
float ctrlTSet = 0;         // mandata calcolata
float ctrlI = 0;            // termine integrale (appreso, salvato in flash)
float ctrlCeil = 0;         // tetto attuale della mandata
bool  ctrlBoost = false;    // recupero: la casa e' molto sotto l'obiettivo da troppo tempo
unsigned long highErrSince = 0;
unsigned long lastRoomMs = 0;    // ultimo aggiornamento della temperatura ambiente
unsigned long lastCtrlWrite = 0;

// ===========================================================================
// Ultimi valori letti (NAN = non ancora noto / non supportato dalla caldaia)
// ===========================================================================
float vFlow = NAN, vRet = NAN, vDhw = NAN, vOut = NAN, vRoom = NAN, vChSet = NAN;
float vPress = NAN, vMod = NAN, vDhwFlow = NAN, vExhaust = NAN, vFaultCode = NAN;
float vDhwSetBoiler = NAN;     // setpoint sanitaria letto dalla caldaia
float vRoomSet = NAN;          // setpoint ambiente del termostato originale (ID 16)
float outdoorMv = NAN;         // tensione grezza letta dalla sonda esterna (diagnostica)
float outdoorOhm = NAN;        // resistenza calcolata della sonda (diagnostica)
float lastThermoDhw = NAN;     // ultimo setpoint sanitaria scritto dal termostato
int8_t bFlame = -1, bCh = -1, bDhw = -1, bFault = -1;   // -1 = sconosciuto
int8_t tCh = -1, tDhw = -1;    // cio' che chiede il termostato (bit di abilitazione)

uint32_t seenIds[4] = {0, 0, 0, 0};     // ID richiesti dal termostato (bitmap 0-127)
bool     unsupported[128] = {false};    // ID rifiutati dalla caldaia
uint8_t  failCount[128] = {0};          // timeout consecutivi per ID (solo richieste extra)
unsigned long lastForwardMs = 0;
bool     pollPending = false;
uint16_t boilerFails = 0;
uint32_t framesOk = 0;

static uint16_t dataOf(unsigned long frame) { return frame & 0xFFFF; }

static unsigned long withData(unsigned long frame, uint16_t data) {
  unsigned long f = (frame & 0x7FFF0000UL) | data;
  if (OpenTherm::parity(f)) f |= 0x80000000UL;
  return f;
}

static void saveSettings() {
  prefs.putBool("ch", cfg.chEnable);
  prefs.putBool("dhw", cfg.dhwEnable);
  prefs.putFloat("chMax", cfg.chMax);
  prefs.putBool("dhwOv", cfg.dhwOverride);
  prefs.putFloat("dhwSet", cfg.dhwSetpoint);
  prefs.putBool("rc", cfg.roomControl);
  prefs.putFloat("rt", cfg.roomTarget);
  prefs.putBool("fa", cfg.flowAuto);
  prefs.putBool("oe", cfg.outdoorEnabled);
}

// aggiorna i valori a partire da un frame valido (richiesta di scrittura o risposta di lettura)
static void ingest(uint8_t id, unsigned long frame) {
  const float f = OpenTherm::getFloat(frame);
  switch (id) {
    case 0: {
      const uint8_t lb = dataOf(frame) & 0xFF;   // stato caldaia: solo dalla risposta
      bFault = lb & 0x01;
      bCh    = (lb >> 1) & 1;
      bDhw   = (lb >> 2) & 1;
      bFlame = (lb >> 3) & 1;
      break;
    }
    case 1:  vChSet = f;                          break;
    case 5:  vFaultCode = dataOf(frame) & 0xFF;   break;   // codice guasto OEM = byte basso
    case 16: vRoomSet = f;                        break;
    case 17: vMod = f;                            break;
    case 18: vPress = f;                          break;
    case 19: vDhwFlow = f;                        break;
    case 24: vRoom = f; lastRoomMs = millis();    break;
    case 25: vFlow = f;                           break;
    case 26: vDhw = f;                            break;
    case 27: vOut = f;                            break;
    case 28: vRet = f;                            break;
    case 33: vExhaust = (int16_t)dataOf(frame);   break;   // intero con segno, non f8.8
    case 56: vDhwSetBoiler = f;                   break;
    default: break;
  }
}

// Sonda esterna NTC: media di 32 letture, filtro esponenziale sul risultato
static void readOutdoor() {
  static unsigned long last = 0;
  const unsigned long now = millis();
  if (!cfg.outdoorEnabled) { vOut = NAN; return; }
  if (last != 0 && now - last < OUTDOOR_READ_MS) return;
  last = now;

  uint32_t sum = 0;
  for (int i = 0; i < 32; i++) { sum += analogReadMilliVolts(PIN_NTC); delayMicroseconds(300); }
  const float mv = sum / 32.0f;
  outdoorMv = mv;
  if (mv < 80 || mv > 3050) { vOut = NAN; outdoorOhm = NAN; return; }   // cortocircuito o sonda scollegata

  const float r = NTC_R_FIXED * mv / (NTC_VCC_MV - mv);
  outdoorOhm = r;
  const float tK = 1.0f / (1.0f / (NTC_T0 + 273.15f) + logf(r / NTC_R0) / NTC_B);
  const float t = tK - 273.15f + NTC_OFFSET_C;
  vOut = isnan(vOut) ? t : vOut + 0.2f * (t - vOut);
}

// Regolatore della temperatura ambiente: isteresi on/off + PI sulla mandata
static void controlStep() {
  static unsigned long last = 0;
  const unsigned long now = millis();
  if (now - last < CTRL_STEP_MS) return;
  last = now;

  const bool fresh = !isnan(vRoom) && lastRoomMs != 0 && (now - lastRoomMs) < ROOM_STALE_MS;
  const bool active = cfg.roomControl && fresh;
  if (active != ctrlActive) {
    Serial.printf("Regolazione ambiente %s\n", active ? "ATTIVA" : (cfg.roomControl ? "sospesa: manca la temperatura ambiente" : "disattivata"));
  }
  ctrlActive = active;

  // "Mandata auto": decide ancora il termostato (quando chiede calore e con il suo obiettivo),
  // il gateway calcola solo la mandata piu' efficiente al posto di quella del termostato.
  const bool flowAuto = !ctrlActive && cfg.flowAuto && fresh && !isnan(vRoomSet) && vRoomSet >= 10 && tCh == 1;
  if (flowAuto != ctrlFlowActive) {
    Serial.printf("Mandata automatica %s\n", flowAuto ? "IN FUNZIONE" : (cfg.flowAuto ? "in attesa (il termostato non chiede calore)" : "disattivata"));
  }
  ctrlFlowActive = flowAuto;
  if (!ctrlActive && !ctrlFlowActive) { ctrlHeat = false; highErrSince = 0; return; }   // l'integrale appreso resta

  const float target = ctrlActive ? cfg.roomTarget : vRoomSet;
  const float err = target - vRoom;
  if (ctrlFlowActive) {
    ctrlHeat = true;                        // il termostato ha gia' deciso che serve calore
  } else if (ctrlHeat) {
    if (err <= -CTRL_OFF_ABOVE) ctrlHeat = false;
  } else if (err >= CTRL_ON_BELOW) {
    ctrlHeat = true;
  }

  // "recupero": se la casa resta molto sotto l'obiettivo a lungo, si concede una mandata piu' alta
  if (err > CTRL_BOOST_ERR) { if (!highErrSince) highErrSince = now; } else { highErrSince = 0; }
  ctrlBoost = highErrSince != 0 && (now - highErrSince) > CTRL_BOOST_AFTER_MS;

  // tetto della mandata: "eco" (condensazione) oppure il limite impostato se in recupero
  // con la sonda esterna il tetto segue la curva climatica, altrimenti e' fisso
  const float ecoMax = (cfg.outdoorEnabled && !isnan(vOut))
      ? fmaxf(CTRL_FLOW_MIN, CTRL_CURVE_BASE + CTRL_CURVE_SLOPE * (CTRL_CURVE_REF - vOut))
      : CTRL_ECO_FLOW_MAX;
  float ceil = ctrlBoost ? cfg.chMax : fminf(cfg.chMax, ecoMax);
  if (!isnan(vRet) && vRet > CTRL_RETURN_MAX) {
    ceil = fmaxf(CTRL_FLOW_MIN, ceil - 2.0f * (vRet - CTRL_RETURN_MAX));   // ritorno troppo caldo: non condensa
  }
  ctrlCeil = ceil;

  if (ctrlHeat) {
    const float dI = err * CTRL_KI * (CTRL_STEP_MS / 60000.0f);
    float I = clampf(ctrlI + dI, 0, CTRL_I_MAX);
    const float t = CTRL_FLOW_MIN + CTRL_KP * err + I;
    if (t > ceil && err > 0) I = ctrlI;      // al tetto: l'integrale non cresce (anti-windup)
    ctrlI = I;
    ctrlTSet = clampf(CTRL_FLOW_MIN + CTRL_KP * err + ctrlI, CTRL_FLOW_MIN, ceil);
  }

  // l'integrale e' cio' che il regolatore ha "imparato" sulla casa: lo salva ogni 10 minuti se e' cambiato
  static unsigned long lastSave = 0;
  static float savedI = -1;
  if (now - lastSave > 600000 && fabsf(ctrlI - savedI) > 0.5f) {
    lastSave = now;
    savedI = ctrlI;
    prefs.putFloat("ctrlI", ctrlI);
  }
  Serial.printf("Regolazione: ambiente=%.1f obiettivo=%.1f err=%+.1f riscaldamento=%s mandata=%.0f tetto=%.0f%s I=%.1f ritorno=%.1f\n",
                vRoom, target, err, ctrlHeat ? "SI" : "no", ctrlHeat ? ctrlTSet : 0.0f, ceil,
                ctrlBoost ? " (recupero)" : "", ctrlI, vRet);
}

// chiamata da sOT.process() quando arriva una richiesta dal termostato
static void processRequest(unsigned long request, OpenThermResponseStatus status) {
  if (status != OpenThermResponseStatus::SUCCESS) return;   // frame corrotto: il termostato ritenta

  const uint8_t  type = (request >> 28) & 0x7;
  const uint8_t  id   = (request >> 16) & 0xFF;
  const uint16_t data = dataOf(request);
  if (id < 128) seenIds[id >> 5] |= 1UL << (id & 31);

  // La caldaia non ha la sonda esterna: se il gateway ne ha una, risponde lui al termostato (ID 27)
  if (id == 27 && type == T_READ_DATA && cfg.outdoorEnabled && !isnan(vOut)) {
    const int16_t raw = (int16_t)roundf(vOut * 256.0f);   // f8.8 con segno (la libreria taglierebbe i negativi)
    sOT.sendResponse(OpenTherm::buildResponse(OpenThermMessageType::READ_ACK, (OpenThermMessageID)27, (uint16_t)raw));
    framesOk++;
    lastForwardMs = millis();
    pollPending = POLL_ENABLED;
    return;
  }

  unsigned long fwd = request;
  bool modified = false;

  if (id == 0 && type == T_READ_DATA) {                       // stato: bit di abilitazione
    tCh  = (data >> 8) & 1;
    tDhw = (data >> 9) & 1;
    uint16_t d = data;
    if (ctrlActive) {                       // regola SmartThings: decide il gateway
      if (ctrlHeat) d |= (1u << 8); else d &= ~(1u << 8);
    }
    if (!cfg.chEnable)  d &= ~(1u << 8);    // interruttore generale: vince su tutto
    if (!cfg.dhwEnable) d &= ~(1u << 9);
    if (d != data) { fwd = withData(request, d); modified = true; }
  } else if (id == 1 && type == T_WRITE_DATA) {               // setpoint mandata
    if (ctrlActive) {
      fwd = withData(request, OpenTherm::temperatureToData(ctrlHeat ? ctrlTSet : 0));
      modified = true;
    } else {
      float t = OpenTherm::getFloat(request);
      if (ctrlFlowActive && t > 0) t = fminf(t, ctrlTSet);        // mandata auto: solo verso il basso
      if (cfg.chMax < CH_MAX_NO_LIMIT && t > cfg.chMax) t = cfg.chMax;
      if (t != OpenTherm::getFloat(request)) {
        fwd = withData(request, OpenTherm::temperatureToData(t));
        modified = true;
      }
    }
  } else if (id == 56 && type == T_WRITE_DATA) {              // setpoint sanitaria
    const float t = OpenTherm::getFloat(request);
    // se il termostato cambia il valore, vince lui: l'override da SmartThings decade
    if (cfg.dhwOverride && !isnan(lastThermoDhw) && fabsf(t - lastThermoDhw) > 0.05f) {
      cfg.dhwOverride = false;
      saveSettings();
    }
    lastThermoDhw = t;
    if (cfg.dhwOverride) {
      fwd = withData(request, OpenTherm::temperatureToData(cfg.dhwSetpoint));
      modified = true;
    }
  }

  unsigned long resp = mOT.sendRequest(fwd);
  const OpenThermResponseStatus rStatus = mOT.getLastResponseStatus();
  const uint8_t rawType = (resp >> 28) & 0x7;
  // La libreria segna INVALID anche le risposte corrette "DATA_INVALID" / "UNKNOWN_DATA_ID":
  // vanno inoltrate al termostato come sono (parita' corretta), altrimenti va in timeout.
  const bool relayable = rStatus == OpenThermResponseStatus::SUCCESS ||
                         (rStatus == OpenThermResponseStatus::INVALID && (rawType == 6 || rawType == 7) &&
                          !OpenTherm::parity(resp));
  if (!relayable) {
    boilerFails++;
    return;   // niente risposta: il termostato va in timeout come se la caldaia fosse spenta
  }
  boilerFails = 0;
  framesOk++;
  if (rStatus == OpenThermResponseStatus::INVALID && rawType == 7 && id < 128 && !unsupported[id]) {
    unsupported[id] = true;
    Serial.printf("Caldaia: ID %u non supportato\n", id);
  }

  // Risposta trasparente: il termostato deve rivedere i dati come li ha inviati lui
  unsigned long out = resp;
  if (modified) {
    if (id == 0) out = (resp & 0x7FFF00FFUL) | (data & 0xFF00);
    else         out = (resp & 0x7FFF0000UL) | data;
    if (OpenTherm::parity(out & 0x7FFFFFFFUL)) out |= 0x80000000UL; else out &= 0x7FFFFFFFUL;
  }
  sOT.sendResponse(out);

  const uint8_t rType = (resp >> 28) & 0x7;
  if (rType == T_READ_ACK || rType == T_WRITE_ACK) {
    ingest(id, type == T_WRITE_DATA ? fwd : resp);
  } else if (type == T_WRITE_DATA && (id == 24 || id == 16)) {
    // temperatura ambiente e setpoint ambiente: la caldaia non li supporta ma il termostato li manda,
    // il valore sta nella richiesta
    ingest(id, request);
  }
  if (LOG_FRAMES) Serial.printf("T%08lX > B%08lX\n", fwd, resp);

  lastForwardMs = millis();
  pollPending = POLL_ENABLED;
}

// ---------------------------------------------------------------------------
// Richieste extra alla caldaia (una per ciclo, nella pausa del termostato)
// ---------------------------------------------------------------------------
// Senza termostato nessuno chiede lo stato (ID 0): lo chiede il gateway.
// Bit di abilitazione inviati: riscaldamento = 0 (nessuna richiesta), sanitaria = secondo l'interruttore.
static void pollStatusIdle() {
  const uint16_t flags = cfg.dhwEnable ? (1u << 9) : 0;
  const unsigned long resp = mOT.sendRequest(
      OpenTherm::buildRequest(OpenThermMessageType::READ_DATA, (OpenThermMessageID)0, flags));
  if (mOT.getLastResponseStatus() == OpenThermResponseStatus::SUCCESS && ((resp >> 28) & 0x7) == T_READ_ACK) {
    ingest(0, resp);
  }
}

static void pollExtra() {
  const unsigned long now = millis();
  bool idleTurn = false;
  if (!pollPending) {
    // nessun termostato attivo (o scollegato): interroga la caldaia da sola, un dato alla volta
    static unsigned long lastIdle = 0;
    if (!(POLL_ENABLED && now - lastForwardMs > IDLE_AFTER_MS && now - lastIdle > IDLE_POLL_MS)) return;
    lastIdle = now;
    idleTurn = true;
    static bool statusTurn = false;
    statusTurn = !statusTurn;                 // un turno su due: stato (fiamma, riscaldamento, sanitaria)
    if (statusTurn) { pollStatusIdle(); return; }
  } else {
    const unsigned long since = now - lastForwardMs;
    if (since < POLL_AFTER_MS) return;
    pollPending = false;
    if (since > POLL_WINDOW_MS) return;   // troppo tardi: salta il turno
  }

  static unsigned long lastDhwWrite = 0;
  static uint8_t idx = 0;

  (void)idleTurn;

  // priorita': riscrivere il setpoint sanitaria forzato
  if (cfg.dhwOverride && (dhwWriteNow || lastDhwWrite == 0 || millis() - lastDhwWrite > DHW_REWRITE_MS)) {
    dhwWriteNow = false;
    lastDhwWrite = millis();
    mOT.sendRequest(OpenTherm::buildRequest(OpenThermMessageType::WRITE_DATA, (OpenThermMessageID)56,
                                            OpenTherm::temperatureToData(cfg.dhwSetpoint)));
    return;
  }
  if (!cfg.dhwOverride) lastDhwWrite = 0;

  // regolazione ambiente attiva: scrive la mandata anche se il termostato non lo fa
  if (ctrlActive && millis() - lastCtrlWrite > CTRL_WRITE_MS) {
    lastCtrlWrite = millis();
    mOT.sendRequest(OpenTherm::buildRequest(OpenThermMessageType::WRITE_DATA, (OpenThermMessageID)1,
                                            OpenTherm::temperatureToData(ctrlHeat ? ctrlTSet : 0)));
    return;
  }

  for (uint8_t n = 0; n < sizeof(POLL_IDS); n++) {
    const uint8_t id = POLL_IDS[idx];
    idx = (idx + 1) % sizeof(POLL_IDS);
    if (unsupported[id]) continue;
    const unsigned long resp = mOT.sendRequest(
        OpenTherm::buildRequest(OpenThermMessageType::READ_DATA, (OpenThermMessageID)id, 0));
    const OpenThermResponseStatus st = mOT.getLastResponseStatus();
    const uint8_t rType = (resp >> 28) & 0x7;
    if (st == OpenThermResponseStatus::SUCCESS) {
      failCount[id] = 0;
      if (rType == T_READ_ACK) ingest(id, resp);
    } else if (st == OpenThermResponseStatus::INVALID && (rType == 6 || rType == 7)) {
      unsupported[id] = true;                  // la caldaia risponde "non supportato / dato non valido"
      Serial.printf("Caldaia: ID %u non supportato\n", id);
    } else if (st == OpenThermResponseStatus::TIMEOUT && ++failCount[id] >= POLL_TIMEOUTS_MAX) {
      unsupported[id] = true;                  // silenzio ripetuto: non insistere (ogni timeout costa 1 s)
      Serial.printf("Caldaia: ID %u senza risposta, escluso\n", id);
    }
    return;   // una sola richiesta per ciclo
  }
}

// ===========================================================================
// API HTTP
// ===========================================================================
WebServer server(HTTP_PORT);

static void putF(JsonObject o, const char *key, float v, int decimals = 1) {
  if (isnan(v)) { o[key] = nullptr; return; }
  const float p = powf(10.0f, decimals);
  o[key] = roundf(v * p) / p;
}

static void putB(JsonObject o, const char *key, int8_t v) {
  if (v < 0) o[key] = nullptr; else o[key] = (v != 0);
}

// setpoint sanitaria "effettivo": forzato, altrimenti quello letto dalla caldaia o scritto dal termostato
static float effectiveDhwSetpoint() {
  if (cfg.dhwOverride) return cfg.dhwSetpoint;
  if (!isnan(vDhwSetBoiler)) return vDhwSetBoiler;
  if (!isnan(lastThermoDhw)) return lastThermoDhw;
  return cfg.dhwSetpoint;   // ancora ignoto: valore di partenza, cosi' lo slider in SmartThings e' utilizzabile
}

static void sendState(int code = 200) {
  JsonDocument doc;
  doc["uptime"] = millis() / 1000;
  doc["frames"] = framesOk;
  doc["boilerFails"] = boilerFails;

  JsonObject s = doc["settings"].to<JsonObject>();
  s["chEnable"] = cfg.chEnable;
  s["dhwEnable"] = cfg.dhwEnable;
  s["chMax"] = cfg.chMax;
  s["chMaxLimited"] = cfg.chMax < CH_MAX_NO_LIMIT;
  s["dhwOverride"] = cfg.dhwOverride;
  putF(s, "dhwSetpoint", effectiveDhwSetpoint());
  s["roomControl"] = cfg.roomControl;
  s["flowAuto"] = cfg.flowAuto;
  s["outdoorEnabled"] = cfg.outdoorEnabled;
  putF(s, "roomTarget", cfg.roomTarget);

  JsonObject c = doc["control"].to<JsonObject>();
  c["active"] = ctrlActive;                        // false = modalita' attiva ma manca la temperatura ambiente
  c["flowAutoActive"] = ctrlFlowActive;            // la mandata e' calcolata dal gateway (il termostato chiede calore)
  c["heating"] = ctrlActive && ctrlHeat;
  putF(c, "flowSetpoint", (ctrlActive || ctrlFlowActive) && ctrlHeat ? ctrlTSet : NAN);
  c["roomTempAgeSeconds"] = lastRoomMs ? (int)((millis() - lastRoomMs) / 1000) : -1;
  putF(c, "integral", ctrlI);
  putF(c, "ceiling", ctrlCeil, 0);
  c["boost"] = ctrlBoost;
  doc["wifiRssi"] = WiFi.RSSI();

  JsonObject b = doc["boiler"].to<JsonObject>();
  putF(b, "flow", vFlow);
  putF(b, "return", vRet);
  putF(b, "dhw", vDhw);
  putF(b, "outside", vOut);
  putF(b, "room", vRoom);
  putF(b, "exhaust", vExhaust, 0);
  putF(b, "modulation", vMod, 0);
  putF(b, "pressure", vPress, 2);
  putF(b, "dhwFlow", vDhwFlow);
  putF(b, "chSetpointSent", vChSet);
  putB(b, "flame", bFlame);
  putB(b, "chActive", bCh);
  putB(b, "dhwActive", bDhw);
  putB(b, "fault", bFault);
  putF(b, "faultCode", vFaultCode, 0);

  JsonObject o = doc["outdoorSensor"].to<JsonObject>();   // diagnostica/taratura della sonda esterna
  o["enabled"] = cfg.outdoorEnabled;
  putF(o, "millivolt", outdoorMv, 0);
  putF(o, "ohm", outdoorOhm, 0);

  JsonObject t = doc["thermostat"].to<JsonObject>();
  putB(t, "chRequest", tCh);
  putB(t, "dhwRequest", tDhw);
  putF(t, "roomSetpoint", vRoomSet);

  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void sendError(int code, const char *msg) {
  String out = String("{\"error\":\"") + msg + "\"}";
  server.send(code, "application/json", out);
}

static void handleSettings() {
  if (strlen(API_TOKEN) > 0 && server.header("X-Token") != API_TOKEN) {
    sendError(401, "token non valido");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "JSON non valido");
    return;
  }
  bool changed = false;
  if (doc["chEnable"].is<bool>()) {
    cfg.chEnable = doc["chEnable"].as<bool>();
    Serial.printf("Riscaldamento %s\n", cfg.chEnable ? "consentito" : "SPENTO");
    changed = true;
  }
  if (doc["dhwEnable"].is<bool>()) {
    cfg.dhwEnable = doc["dhwEnable"].as<bool>();
    Serial.printf("Acqua sanitaria %s\n", cfg.dhwEnable ? "consentita" : "SPENTA");
    changed = true;
  }
  if (doc["chMax"].is<float>()) {
    cfg.chMax = clampf(doc["chMax"].as<float>(), CH_MAX_MIN, CH_MAX_NO_LIMIT);
    Serial.printf("Limite mandata: %.0f C%s\n", cfg.chMax, cfg.chMax >= CH_MAX_NO_LIMIT ? " (nessun limite)" : "");
    changed = true;
  }
  if (doc["roomTarget"].is<float>()) {
    cfg.roomTarget = clampf(doc["roomTarget"].as<float>(), ROOM_TARGET_MIN, ROOM_TARGET_MAX);
    Serial.printf("Obiettivo ambiente: %.1f C\n", cfg.roomTarget);
    changed = true;
  }
  if (doc["outdoorEnabled"].is<bool>()) {
    cfg.outdoorEnabled = doc["outdoorEnabled"].as<bool>();
    Serial.printf("Sonda esterna: %s\n", cfg.outdoorEnabled ? "attiva" : "disattivata");
    changed = true;
  }
  if (doc["flowAuto"].is<bool>()) {
    cfg.flowAuto = doc["flowAuto"].as<bool>();
    if (!cfg.flowAuto) ctrlFlowActive = false;
    Serial.printf("Mandata automatica (eco): %s\n", cfg.flowAuto ? "attiva" : "spenta");
    changed = true;
  }
  if (doc["roomControl"].is<bool>()) {
    cfg.roomControl = doc["roomControl"].as<bool>();
    if (!cfg.roomControl) { ctrlActive = false; ctrlHeat = false; }
    Serial.printf("Regolazione ambiente da SmartThings: %s\n", cfg.roomControl ? "richiesta" : "spenta (segue il termostato)");
    changed = true;
  }
  if (doc["dhwSetpoint"].is<float>()) {
    cfg.dhwSetpoint = clampf(doc["dhwSetpoint"].as<float>(), DHW_SET_MIN, DHW_SET_MAX);
    cfg.dhwOverride = true;
    dhwWriteNow = true;
    Serial.printf("Setpoint sanitaria forzato: %.0f C\n", cfg.dhwSetpoint);
    changed = true;
  }
  if (changed) saveSettings();
  sendState();
}

static void handleIds() {
  JsonDocument doc;
  JsonArray seen = doc["requestedByThermostat"].to<JsonArray>();
  for (int i = 0; i < 128; i++)
    if (seenIds[i >> 5] & (1UL << (i & 31))) seen.add(i);
  JsonArray unsup = doc["unsupportedByBoiler"].to<JsonArray>();
  for (int i = 0; i < 128; i++)
    if (unsupported[i]) unsup.add(i);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Diagnostica: invia UNA richiesta di lettura alla caldaia (ID 1-127, non lo stato) e mostra la risposta grezza.
static void handleProbe() {
  const int id = server.hasArg("id") ? server.arg("id").toInt() : 25;
  if (id < 1 || id > 127) { sendError(400, "id non valido (1-127)"); return; }

  const unsigned long resp = mOT.sendRequest(
      OpenTherm::buildRequest(OpenThermMessageType::READ_DATA, (OpenThermMessageID)id, 0));
  const OpenThermResponseStatus st = mOT.getLastResponseStatus();

  JsonDocument doc;
  doc["id"] = id;
  doc["status"] = st == OpenThermResponseStatus::SUCCESS ? "SUCCESS"
                : st == OpenThermResponseStatus::TIMEOUT ? "TIMEOUT"
                : st == OpenThermResponseStatus::INVALID ? "INVALID" : "NONE";
  if (st == OpenThermResponseStatus::SUCCESS || st == OpenThermResponseStatus::INVALID) {
    char hex[9];
    snprintf(hex, sizeof(hex), "%08lX", resp);
    doc["frame"] = hex;
    doc["type"] = (resp >> 28) & 0x7;      // 4 = READ_ACK, 6 = DATA_INVALID, 7 = UNKNOWN_DATA_ID
    doc["data"] = dataOf(resp);
    putF(doc.as<JsonObject>(), "asFloat", OpenTherm::getFloat(resp), 2);
  }
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ===========================================================================
// Rete
// ===========================================================================
bool netUp = false;
bool otaBusy = false;

static void handleNetwork() {
  static unsigned long lastTry = 0;
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected) {
    if (netUp) { netUp = false; Serial.println("Wi-Fi perso"); }
    if (millis() - lastTry > 15000) {
      lastTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }
  if (!netUp) {
    netUp = true;
    Serial.printf("Wi-Fi collegato: %s\n", WiFi.localIP().toString().c_str());
    MDNS.begin(HOSTNAME);
    ArduinoOTA.setHostname(HOSTNAME);
    // durante l'aggiornamento il gateway smette di inoltrare (il termostato va in timeout per qualche
    // secondo): senza i blocchi dell'inoltro la ricezione del firmware e' molto piu' veloce
    ArduinoOTA.onStart([]() { otaBusy = true; Serial.println("Aggiornamento OTA..."); });
    ArduinoOTA.onEnd([]() { otaBusy = false; });
    ArduinoOTA.onError([](ota_error_t) { otaBusy = false; });
    if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.begin();
  }
  ArduinoOTA.handle();
}

// ===========================================================================
static void statusLine() {
  static unsigned long last = 0;
  if (millis() - last < 30000) return;
  last = millis();
  Serial.printf("[%lus] frame ok=%lu, errori caldaia consecutivi=%u, heap=%u | mandata=%.1f ritorno=%.1f acs=%.1f mod=%.0f%% press=%.2f | fiamma=%d ch=%d acs=%d guasto=%d\n",
                millis() / 1000, framesOk, boilerFails, ESP.getFreeHeap(),
                vFlow, vRet, vDhw, vMod, vPress, bFlame, bCh, bDhw, bFault);
  Serial.print("ID richiesti dal termostato:");
  for (int i = 0; i < 128; i++)
    if (seenIds[i >> 5] & (1UL << (i & 31))) Serial.printf(" %d", i);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nGateway OpenTherm");

  prefs.begin("otgw", false);
  cfg.chEnable    = prefs.getBool("ch", true);
  cfg.dhwEnable   = prefs.getBool("dhw", true);
  cfg.chMax       = prefs.getFloat("chMax", CH_MAX_NO_LIMIT);
  cfg.dhwOverride = prefs.getBool("dhwOv", false);
  cfg.dhwSetpoint = prefs.getFloat("dhwSet", 50);
  cfg.roomControl = prefs.getBool("rc", false);
  cfg.roomTarget  = prefs.getFloat("rt", 20);
  cfg.flowAuto    = prefs.getBool("fa", false);
  cfg.outdoorEnabled = prefs.getBool("oe", false);
  analogSetAttenuation(ADC_11db);   // ingresso della sonda esterna fino a ~3,1 V
  ctrlI           = prefs.getFloat("ctrlI", 0);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // gateway sempre alimentato: il risparmio energetico Wi-Fi rallenta le risposte
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const char *headers[] = {"X-Token"};
  server.collectHeaders(headers, 1);
  server.on("/api/state", HTTP_GET, []() { sendState(); });
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/ids", HTTP_GET, handleIds);
  server.on("/api/probe", HTTP_GET, handleProbe);
  server.onNotFound([]() { sendError(404, "non trovato"); });
  server.begin();

  mOT.begin(mIsr);
  sOT.begin(sIsr, processRequest);
  Serial.println("Gateway OpenTherm attivo");
}

void loop() {
  if (otaBusy) { ArduinoOTA.handle(); return; }   // aggiornamento in corso: solo ricezione
  sOT.process();
  readOutdoor();
  controlStep();
  pollExtra();
  server.handleClient();
  handleNetwork();
  statusLine();
  delay(1);
}
