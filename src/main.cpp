// OpenTherm gateway with an HTTP API (for the SmartThings Edge driver in driver/)
//
//   original thermostat <-> [slave shield] C3 mini [master shield] <-> boiler
//
// The gateway relays every message transparently. In addition it:
//  - reads all the data that passes through and exposes the state as JSON;
//  - can switch heating / domestic hot water off (clears the bits in the status message);
//  - can cap the flow temperature requested by the thermostat;
//  - can force the domestic hot water setpoint;
//  - in the gaps between thermostat cycles, asks the boiler for data the thermostat does not request.
//
// OpenTherm relaying works even when Wi-Fi is not connected.
//
// API:
//   GET  /api/state      full state
//   POST /api/settings   JSON with one or more of: chEnable, dhwEnable, chMax, dhwSetpoint,
//                        roomControl, roomTarget, flowAuto, outdoorEnabled
//   GET  /api/ids        OpenTherm IDs seen (debug)
//   GET  /api/probe?id=N read-only test request of one ID

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
OpenTherm mOT(PIN_M_IN, PIN_M_OUT);         // talks to the boiler (acts as the thermostat)
OpenTherm sOT(PIN_S_IN, PIN_S_OUT, true);   // talks to the thermostat (acts as the boiler)

void IRAM_ATTR mIsr() { mOT.handleInterrupt(); }
void IRAM_ATTR sIsr() { sOT.handleInterrupt(); }

// message types (bits 28-30)
constexpr uint8_t T_READ_DATA = 0, T_WRITE_DATA = 1, T_READ_ACK = 4, T_WRITE_ACK = 5, T_UNKNOWN_ID = 7;

// ===========================================================================
// Settings changeable from SmartThings (stored in flash)
// ===========================================================================
struct Settings {
  bool  chEnable    = true;              // heating allowed
  bool  dhwEnable   = true;              // domestic hot water allowed
  float chMax       = CH_MAX_NO_LIMIT;   // flow temperature cap (CH_MAX_NO_LIMIT = no cap)
  bool  dhwOverride = false;             // hot water setpoint forced from SmartThings
  float dhwSetpoint = 50;                // forced value (or last known)
  bool  roomControl = false;             // true = the gateway regulates the room towards the SmartThings target
  float roomTarget  = 20;                // desired room temperature
  bool  flowAuto    = false;             // automatic (eco) flow temperature while the original thermostat is in command
  bool  outdoorEnabled = false;          // outdoor probe connected (enable only after wiring the probe)
} cfg;

Preferences prefs;
bool dhwWriteNow = false;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// room regulator state (updated by controlStep)
bool  ctrlActive = false;   // true = actually in command (mode on and room temperature fresh)
bool  ctrlHeat = false;     // computed heating request
bool  ctrlFlowActive = false;   // automatic flow active (the original thermostat decides when to heat)
float ctrlTSet = 0;         // computed flow temperature
float ctrlI = 0;            // integral term (learned, stored in flash)
float ctrlCeil = 0;         // current flow temperature ceiling
bool  ctrlBoost = false;    // recovery: the house has been well below target for too long
unsigned long highErrSince = 0;
unsigned long lastRoomMs = 0;    // last room temperature update
unsigned long lastCtrlWrite = 0;

// ===========================================================================
// Last values read (NAN = not known yet / not supported by the boiler)
// ===========================================================================
float vFlow = NAN, vRet = NAN, vDhw = NAN, vOut = NAN, vRoom = NAN, vChSet = NAN;
float vPress = NAN, vMod = NAN, vDhwFlow = NAN, vExhaust = NAN, vFaultCode = NAN;
float vDhwSetBoiler = NAN;     // hot water setpoint read from the boiler
float vRoomSet = NAN;          // room setpoint of the original thermostat (ID 16)
float outdoorMv = NAN;         // raw voltage read from the outdoor probe (diagnostics)
float outdoorOhm = NAN;        // computed probe resistance (diagnostics)
float lastThermoDhw = NAN;     // last hot water setpoint written by the thermostat
int8_t bFlame = -1, bCh = -1, bDhw = -1, bFault = -1;   // -1 = unknown
int8_t tCh = -1, tDhw = -1;    // what the thermostat asks for (enable bits)

uint32_t seenIds[4] = {0, 0, 0, 0};     // IDs requested by the thermostat (bitmap 0-127)
bool     unsupported[128] = {false};    // IDs rejected by the boiler
uint8_t  failCount[128] = {0};          // consecutive timeouts per ID (extra requests only)
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

// update the values from a valid frame (write request or read response)
static void ingest(uint8_t id, unsigned long frame) {
  const float f = OpenTherm::getFloat(frame);
  switch (id) {
    case 0: {
      const uint8_t lb = dataOf(frame) & 0xFF;   // boiler status: from the response only
      bFault = lb & 0x01;
      bCh    = (lb >> 1) & 1;
      bDhw   = (lb >> 2) & 1;
      bFlame = (lb >> 3) & 1;
      break;
    }
    case 1:  vChSet = f;                          break;
    case 5:  vFaultCode = dataOf(frame) & 0xFF;   break;   // OEM fault code = low byte
    case 16: vRoomSet = f;                        break;
    case 17: vMod = f;                            break;
    case 18: vPress = f;                          break;
    case 19: vDhwFlow = f;                        break;
    case 24: vRoom = f; lastRoomMs = millis();    break;
    case 25: vFlow = f;                           break;
    case 26: vDhw = f;                            break;
    case 27: vOut = f;                            break;
    case 28: vRet = f;                            break;
    case 33: vExhaust = (int16_t)dataOf(frame);   break;   // signed integer, not f8.8
    case 56: vDhwSetBoiler = f;                   break;
    default: break;
  }
}

// NTC outdoor probe: average of 32 readings, exponential filter on the result
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
  if (mv < 80 || mv > 3050) { vOut = NAN; outdoorOhm = NAN; return; }   // short circuit or probe disconnected

  const float r = NTC_R_FIXED * mv / (NTC_VCC_MV - mv);
  outdoorOhm = r;
  const float tK = 1.0f / (1.0f / (NTC_T0 + 273.15f) + logf(r / NTC_R0) / NTC_B);
  const float t = tK - 273.15f + NTC_OFFSET_C;
  vOut = isnan(vOut) ? t : vOut + 0.2f * (t - vOut);
}

// Room temperature regulator: on/off hysteresis + PI on the flow temperature
static void controlStep() {
  static unsigned long last = 0;
  const unsigned long now = millis();
  if (now - last < CTRL_STEP_MS) return;
  last = now;

  const bool fresh = !isnan(vRoom) && lastRoomMs != 0 && (now - lastRoomMs) < ROOM_STALE_MS;
  const bool active = cfg.roomControl && fresh;
  if (active != ctrlActive) {
    Serial.printf("Room regulation %s\n", active ? "ACTIVE" : (cfg.roomControl ? "suspended: no room temperature" : "off"));
  }
  ctrlActive = active;

  // "Auto flow": the thermostat still decides (when it asks for heat, with its own target),
  // the gateway only computes the most efficient flow temperature instead of the thermostat's.
  const bool flowAuto = !ctrlActive && cfg.flowAuto && fresh && !isnan(vRoomSet) && vRoomSet >= 10 && tCh == 1;
  if (flowAuto != ctrlFlowActive) {
    Serial.printf("Auto flow %s\n", flowAuto ? "RUNNING" : (cfg.flowAuto ? "waiting (thermostat is not asking for heat)" : "off"));
  }
  ctrlFlowActive = flowAuto;
  if (!ctrlActive && !ctrlFlowActive) { ctrlHeat = false; highErrSince = 0; return; }   // the learned integral is kept

  const float target = ctrlActive ? cfg.roomTarget : vRoomSet;
  const float err = target - vRoom;
  if (ctrlFlowActive) {
    ctrlHeat = true;                        // the thermostat has already decided heat is needed
  } else if (ctrlHeat) {
    if (err <= -CTRL_OFF_ABOVE) ctrlHeat = false;
  } else if (err >= CTRL_ON_BELOW) {
    ctrlHeat = true;
  }

  // "recovery": if the house stays far below target for long, allow a higher flow temperature
  if (err > CTRL_BOOST_ERR) { if (!highErrSince) highErrSince = now; } else { highErrSince = 0; }
  ctrlBoost = highErrSince != 0 && (now - highErrSince) > CTRL_BOOST_AFTER_MS;

  // flow ceiling: "eco" (condensing) or the configured cap when recovering
  // with the outdoor probe the ceiling follows the heating curve, otherwise it is fixed
  const float ecoMax = (cfg.outdoorEnabled && !isnan(vOut))
      ? fmaxf(CTRL_FLOW_MIN, CTRL_CURVE_BASE + CTRL_CURVE_SLOPE * (CTRL_CURVE_REF - vOut))
      : CTRL_ECO_FLOW_MAX;
  float ceil = ctrlBoost ? cfg.chMax : fminf(cfg.chMax, ecoMax);
  if (!isnan(vRet) && vRet > CTRL_RETURN_MAX) {
    ceil = fmaxf(CTRL_FLOW_MIN, ceil - 2.0f * (vRet - CTRL_RETURN_MAX));   // return too hot: not condensing
  }
  ctrlCeil = ceil;

  if (ctrlHeat) {
    const float dI = err * CTRL_KI * (CTRL_STEP_MS / 60000.0f);
    float I = clampf(ctrlI + dI, 0, CTRL_I_MAX);
    const float t = CTRL_FLOW_MIN + CTRL_KP * err + I;
    if (t > ceil && err > 0) I = ctrlI;      // at the ceiling: the integral does not grow (anti-windup)
    ctrlI = I;
    ctrlTSet = clampf(CTRL_FLOW_MIN + CTRL_KP * err + ctrlI, CTRL_FLOW_MIN, ceil);
  }

  // the integral is what the regulator has "learned" about the house: saved every 10 minutes if changed
  static unsigned long lastSave = 0;
  static float savedI = -1;
  if (now - lastSave > 600000 && fabsf(ctrlI - savedI) > 0.5f) {
    lastSave = now;
    savedI = ctrlI;
    prefs.putFloat("ctrlI", ctrlI);
  }
  Serial.printf("Regulation: room=%.1f target=%.1f err=%+.1f heating=%s flow=%.0f ceiling=%.0f%s I=%.1f return=%.1f\n",
                vRoom, target, err, ctrlHeat ? "YES" : "no", ctrlHeat ? ctrlTSet : 0.0f, ceil,
                ctrlBoost ? " (recovery)" : "", ctrlI, vRet);
}

// called by sOT.process() when a request arrives from the thermostat
static void processRequest(unsigned long request, OpenThermResponseStatus status) {
  if (status != OpenThermResponseStatus::SUCCESS) return;   // corrupted frame: the thermostat retries

  const uint8_t  type = (request >> 28) & 0x7;
  const uint8_t  id   = (request >> 16) & 0xFF;
  const uint16_t data = dataOf(request);
  if (id < 128) seenIds[id >> 5] |= 1UL << (id & 31);

  // The boiler has no outdoor probe: if the gateway has one, it answers the thermostat itself (ID 27)
  if (id == 27 && type == T_READ_DATA && cfg.outdoorEnabled && !isnan(vOut)) {
    const int16_t raw = (int16_t)roundf(vOut * 256.0f);   // signed f8.8 (the library would clamp negatives)
    sOT.sendResponse(OpenTherm::buildResponse(OpenThermMessageType::READ_ACK, (OpenThermMessageID)27, (uint16_t)raw));
    framesOk++;
    lastForwardMs = millis();
    pollPending = POLL_ENABLED;
    return;
  }

  unsigned long fwd = request;
  bool modified = false;

  if (id == 0 && type == T_READ_DATA) {                       // status: enable bits
    tCh  = (data >> 8) & 1;
    tDhw = (data >> 9) & 1;
    uint16_t d = data;
    if (ctrlActive) {                       // SmartThings regulates: the gateway decides
      if (ctrlHeat) d |= (1u << 8); else d &= ~(1u << 8);
    }
    if (!cfg.chEnable)  d &= ~(1u << 8);    // master switch: wins over everything
    if (!cfg.dhwEnable) d &= ~(1u << 9);
    if (d != data) { fwd = withData(request, d); modified = true; }
  } else if (id == 1 && type == T_WRITE_DATA) {               // flow temperature setpoint
    if (ctrlActive) {
      fwd = withData(request, OpenTherm::temperatureToData(ctrlHeat ? ctrlTSet : 0));
      modified = true;
    } else {
      float t = OpenTherm::getFloat(request);
      if (ctrlFlowActive && t > 0) t = fminf(t, ctrlTSet);        // auto flow: downwards only
      if (cfg.chMax < CH_MAX_NO_LIMIT && t > cfg.chMax) t = cfg.chMax;
      if (t != OpenTherm::getFloat(request)) {
        fwd = withData(request, OpenTherm::temperatureToData(t));
        modified = true;
      }
    }
  } else if (id == 56 && type == T_WRITE_DATA) {              // hot water setpoint
    const float t = OpenTherm::getFloat(request);
    // if the thermostat changes the value, it wins: the SmartThings override is dropped
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
  // The library marks the valid "DATA_INVALID" / "UNKNOWN_DATA_ID" responses as INVALID too:
  // they must be relayed to the thermostat as they are (parity OK), otherwise it times out.
  const bool relayable = rStatus == OpenThermResponseStatus::SUCCESS ||
                         (rStatus == OpenThermResponseStatus::INVALID && (rawType == 6 || rawType == 7) &&
                          !OpenTherm::parity(resp));
  if (!relayable) {
    boilerFails++;
    return;   // no response: the thermostat times out as if the boiler were off
  }
  boilerFails = 0;
  framesOk++;
  if (rStatus == OpenThermResponseStatus::INVALID && rawType == 7 && id < 128 && !unsupported[id]) {
    unsupported[id] = true;
    Serial.printf("Boiler: ID %u not supported\n", id);
  }

  // Transparent response: the thermostat must see the data as it sent it
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
    // room temperature and room setpoint: the boiler does not support them but the thermostat sends them,
    // the value is in the request
    ingest(id, request);
  }
  if (LOG_FRAMES) Serial.printf("T%08lX > B%08lX\n", fwd, resp);

  lastForwardMs = millis();
  pollPending = POLL_ENABLED;
}

// ---------------------------------------------------------------------------
// Extra requests to the boiler (one per cycle, in the thermostat's pause)
// ---------------------------------------------------------------------------
// Without a thermostat nobody asks for the status (ID 0): the gateway does.
// Enable bits sent: heating = 0 (no request), hot water = according to the switch.
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
    // no thermostat active (or disconnected): query the boiler alone, one value at a time
    static unsigned long lastIdle = 0;
    if (!(POLL_ENABLED && now - lastForwardMs > IDLE_AFTER_MS && now - lastIdle > IDLE_POLL_MS)) return;
    lastIdle = now;
    idleTurn = true;
    static bool statusTurn = false;
    statusTurn = !statusTurn;                 // every other turn: status (flame, heating, hot water)
    if (statusTurn) { pollStatusIdle(); return; }
  } else {
    const unsigned long since = now - lastForwardMs;
    if (since < POLL_AFTER_MS) return;
    pollPending = false;
    if (since > POLL_WINDOW_MS) return;   // too late: skip this turn
  }

  static unsigned long lastDhwWrite = 0;
  static uint8_t idx = 0;

  (void)idleTurn;

  // priority: rewrite the forced hot water setpoint
  if (cfg.dhwOverride && (dhwWriteNow || lastDhwWrite == 0 || millis() - lastDhwWrite > DHW_REWRITE_MS)) {
    dhwWriteNow = false;
    lastDhwWrite = millis();
    mOT.sendRequest(OpenTherm::buildRequest(OpenThermMessageType::WRITE_DATA, (OpenThermMessageID)56,
                                            OpenTherm::temperatureToData(cfg.dhwSetpoint)));
    return;
  }
  if (!cfg.dhwOverride) lastDhwWrite = 0;

  // room regulation active: write the flow temperature even if the thermostat does not
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
      unsupported[id] = true;                  // the boiler answers "not supported / invalid data"
      Serial.printf("Boiler: ID %u not supported\n", id);
    } else if (st == OpenThermResponseStatus::TIMEOUT && ++failCount[id] >= POLL_TIMEOUTS_MAX) {
      unsupported[id] = true;                  // repeated silence: stop trying (every timeout costs 1 s)
      Serial.printf("Boiler: ID %u gives no answer, excluded\n", id);
    }
    return;   // only one request per cycle
  }
}

// ===========================================================================
// HTTP API
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

// "effective" hot water setpoint: forced, otherwise the one read from the boiler or written by the thermostat
static float effectiveDhwSetpoint() {
  if (cfg.dhwOverride) return cfg.dhwSetpoint;
  if (!isnan(vDhwSetBoiler)) return vDhwSetBoiler;
  if (!isnan(lastThermoDhw)) return lastThermoDhw;
  return cfg.dhwSetpoint;   // still unknown: starting value, so the SmartThings slider is usable
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
  c["active"] = ctrlActive;                        // false = mode on but no room temperature
  c["flowAutoActive"] = ctrlFlowActive;            // the flow temperature is computed by the gateway (the thermostat asks for heat)
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

  JsonObject o = doc["outdoorSensor"].to<JsonObject>();   // outdoor probe diagnostics/calibration
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
    sendError(401, "invalid token");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    sendError(400, "invalid JSON");
    return;
  }
  bool changed = false;
  if (doc["chEnable"].is<bool>()) {
    cfg.chEnable = doc["chEnable"].as<bool>();
    Serial.printf("Heating %s\n", cfg.chEnable ? "allowed" : "OFF");
    changed = true;
  }
  if (doc["dhwEnable"].is<bool>()) {
    cfg.dhwEnable = doc["dhwEnable"].as<bool>();
    Serial.printf("Hot water %s\n", cfg.dhwEnable ? "allowed" : "OFF");
    changed = true;
  }
  if (doc["chMax"].is<float>()) {
    cfg.chMax = clampf(doc["chMax"].as<float>(), CH_MAX_MIN, CH_MAX_NO_LIMIT);
    Serial.printf("Flow cap: %.0f C%s\n", cfg.chMax, cfg.chMax >= CH_MAX_NO_LIMIT ? " (no cap)" : "");
    changed = true;
  }
  if (doc["roomTarget"].is<float>()) {
    cfg.roomTarget = clampf(doc["roomTarget"].as<float>(), ROOM_TARGET_MIN, ROOM_TARGET_MAX);
    Serial.printf("Room target: %.1f C\n", cfg.roomTarget);
    changed = true;
  }
  if (doc["outdoorEnabled"].is<bool>()) {
    cfg.outdoorEnabled = doc["outdoorEnabled"].as<bool>();
    Serial.printf("Outdoor probe: %s\n", cfg.outdoorEnabled ? "enabled" : "disabled");
    changed = true;
  }
  if (doc["flowAuto"].is<bool>()) {
    cfg.flowAuto = doc["flowAuto"].as<bool>();
    if (!cfg.flowAuto) ctrlFlowActive = false;
    Serial.printf("Auto flow (eco): %s\n", cfg.flowAuto ? "on" : "off");
    changed = true;
  }
  if (doc["roomControl"].is<bool>()) {
    cfg.roomControl = doc["roomControl"].as<bool>();
    if (!cfg.roomControl) { ctrlActive = false; ctrlHeat = false; }
    Serial.printf("Room regulation from SmartThings: %s\n", cfg.roomControl ? "requested" : "off (follows the thermostat)");
    changed = true;
  }
  if (doc["dhwSetpoint"].is<float>()) {
    cfg.dhwSetpoint = clampf(doc["dhwSetpoint"].as<float>(), DHW_SET_MIN, DHW_SET_MAX);
    cfg.dhwOverride = true;
    dhwWriteNow = true;
    Serial.printf("Hot water setpoint forced: %.0f C\n", cfg.dhwSetpoint);
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

// Diagnostics: sends ONE read request to the boiler (ID 1-127, not the status) and shows the raw response.
static void handleProbe() {
  const int id = server.hasArg("id") ? server.arg("id").toInt() : 25;
  if (id < 1 || id > 127) { sendError(400, "invalid id (1-127)"); return; }

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
// Network
// ===========================================================================
bool netUp = false;
bool otaBusy = false;

static void handleNetwork() {
  static unsigned long lastTry = 0;
  const bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected) {
    if (netUp) { netUp = false; Serial.println("Wi-Fi lost"); }
    if (millis() - lastTry > 15000) {
      lastTry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }
  if (!netUp) {
    netUp = true;
    Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
    MDNS.begin(HOSTNAME);
    ArduinoOTA.setHostname(HOSTNAME);
    // during the update the gateway stops relaying (the thermostat times out for a few
    // seconds): without the relaying stalls the firmware is received much faster
    ArduinoOTA.onStart([]() { otaBusy = true; Serial.println("OTA update..."); });
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
  Serial.printf("[%lus] frames ok=%lu, consecutive boiler errors=%u, heap=%u | flow=%.1f return=%.1f dhw=%.1f mod=%.0f%% press=%.2f | flame=%d ch=%d dhw=%d fault=%d\n",
                millis() / 1000, framesOk, boilerFails, ESP.getFreeHeap(),
                vFlow, vRet, vDhw, vMod, vPress, bFlame, bCh, bDhw, bFault);
  Serial.print("IDs requested by the thermostat:");
  for (int i = 0; i < 128; i++)
    if (seenIds[i >> 5] & (1UL << (i & 31))) Serial.printf(" %d", i);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\nOpenTherm gateway");

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
  analogSetAttenuation(ADC_11db);   // outdoor probe input up to ~3.1 V
  ctrlI           = prefs.getFloat("ctrlI", 0);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);   // always-powered gateway: Wi-Fi power saving slows the responses
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const char *headers[] = {"X-Token"};
  server.collectHeaders(headers, 1);
  server.on("/api/state", HTTP_GET, []() { sendState(); });
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/ids", HTTP_GET, handleIds);
  server.on("/api/probe", HTTP_GET, handleProbe);
  server.onNotFound([]() { sendError(404, "not found"); });
  server.begin();

  mOT.begin(mIsr);
  sOT.begin(sIsr, processRequest);
  Serial.println("OpenTherm gateway running");
}

void loop() {
  if (otaBusy) { ArduinoOTA.handle(); return; }   // update in progress: receive only
  sOT.process();
  readOutdoor();
  controlStep();
  pollExtra();
  server.handleClient();
  handleNetwork();
  statusLine();
  delay(1);
}
