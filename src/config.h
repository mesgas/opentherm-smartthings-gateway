#pragma once

// ---------------------------------------------------------------------------
// LOLIN C3 mini pins (D1 mini positions of the DIYLess shields)
// ---------------------------------------------------------------------------
constexpr int PIN_M_IN  = 8;   // D2  master shield (towards the boiler): OT input
constexpr int PIN_M_OUT = 10;  // D1  master shield: OT output
constexpr int PIN_S_IN  = 0;   // D6  slave shield (towards the thermostat): OT input
constexpr int PIN_S_OUT = 4;   // D7  slave shield: OT output

// ---------------------------------------------------------------------------
// NTC outdoor probe (TEWA TT05-10KC8-1S-T105-1500: 10 kOhm at 25 C, B25/85 = 3435 K)
// Divider: 3V3 - R_FIXED - [node -> PIN_NTC] - NTC - GND, with 100 nF between node and GND
// ---------------------------------------------------------------------------
constexpr int   PIN_NTC        = 3;       // "A0" position of the D1 mini = GPIO3 (ADC1)
constexpr float NTC_R_FIXED    = 10000;   // fixed resistor to 3V3 (ohm), preferably 0.1-1 %
constexpr float NTC_R0         = 10000;   // probe resistance at NTC_T0
constexpr float NTC_T0         = 25;
constexpr float NTC_B          = 3435;
constexpr float NTC_VCC_MV     = 3300;    // real voltage of the 3V3 pin: measure it with a multimeter and fix it here
constexpr float NTC_OFFSET_C   = 0;       // final correction in C (compare with a reference thermometer)
constexpr unsigned long OUTDOOR_READ_MS = 10000;

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
#define HOSTNAME "otgw"        // http://otgw.local/api/state
constexpr uint16_t HTTP_PORT = 80;

// ---------------------------------------------------------------------------
// Setpoint limits: adapt them to your boiler (here: heating 30-85 C, hot water 38-60 C)
// ---------------------------------------------------------------------------
constexpr float CH_MAX_MIN      = 30;   // flow cap: lowest settable value
constexpr float CH_MAX_NO_LIMIT = 85;   // at this value the cap is disabled
constexpr float DHW_SET_MIN     = 38;
constexpr float DHW_SET_MAX     = 60;

// ---------------------------------------------------------------------------
// Room temperature control from SmartThings ("app command" mode)
// The gateway replaces the thermostat: it switches heating on/off with
// hysteresis and computes the flow temperature with a PI regulator on the room
// temperature (ID 24, which the thermostat sends to the boiler). To be tuned on the real system.
// ---------------------------------------------------------------------------
constexpr float ROOM_TARGET_MIN = 10;
constexpr float ROOM_TARGET_MAX = 30;
constexpr float CTRL_ON_BELOW   = 0.3;   // switches on if room < target - 0.3 C
constexpr float CTRL_OFF_ABOVE  = 0.2;   // switches off if room > target + 0.2 C
constexpr float CTRL_FLOW_MIN   = 30;    // minimum flow temperature while heating is on
constexpr float CTRL_KP         = 10;    // C of flow temperature per C of error
constexpr float CTRL_KI         = 0.15;  // C of flow temperature per (C of error x minute)
constexpr float CTRL_I_MAX      = 25;    // limit of the integral term (C of flow temperature)
// Efficiency (condensing boiler): low flow = return below ~55 C = condensing
constexpr float CTRL_ECO_FLOW_MAX = 50;  // "normal" ceiling of the computed flow temperature
constexpr float CTRL_BOOST_ERR    = 1.0; // if the house is colder than this for CTRL_BOOST_AFTER_MS, the ceiling rises to the cap
constexpr unsigned long CTRL_BOOST_AFTER_MS = 1800000;   // 30 minutes
constexpr float CTRL_FIRE_MARGIN  = 3;   // when far below target the ceiling follows the REAL flow temperature + this margin,
                                         // so the burner actually fires (the flow then climbs gradually)
constexpr float CTRL_RETURN_MAX   = 55;  // above this return temperature the flow is reduced (no condensing)
// Heating curve (only with the outdoor probe enabled): ceiling = BASE + SLOPE x (REF - outdoor temperature)
// With the values below: 20 C outside = 30, 10 C = 42, 0 C = 54, -5 C = 60. To be tuned on the real system.
constexpr float CTRL_CURVE_BASE   = 30;
constexpr float CTRL_CURVE_SLOPE  = 1.2;
constexpr float CTRL_CURVE_REF    = 20;
constexpr unsigned long CTRL_STEP_MS   = 30000;    // regulator period
constexpr unsigned long CTRL_WRITE_MS  = 15000;    // rewrites the flow temperature to the boiler at this rate
constexpr unsigned long ROOM_STALE_MS  = 600000;   // no room temperature for 10 min: back to the thermostat

// ---------------------------------------------------------------------------
// Gateway
// ---------------------------------------------------------------------------
// Extra requests to the boiler, sent in the pause between two thermostat cycles.
// List obtained by testing a condensing boiler: 18, 19, 27 not supported; 33 gives no answer.
// Every boiler is different: try your own IDs with /api/probe?id=N and adapt the list.
// IDs: 25 flow, 28 return, 26 hot water, 17 modulation, 5 faults, 56 hot water setpoint
constexpr uint8_t       POLL_IDS[]      = {25, 28, 26, 17, 5, 56};
constexpr uint8_t       POLL_TIMEOUTS_MAX = 5;   // consecutive timeouts before excluding an ID
constexpr bool          POLL_ENABLED    = true;
// If the thermostat has been silent for IDLE_AFTER_MS, the gateway queries the boiler by itself (one value every IDLE_POLL_MS)
constexpr unsigned long IDLE_AFTER_MS   = 5000;
constexpr unsigned long IDLE_POLL_MS    = 2000;
constexpr unsigned long POLL_AFTER_MS   = 350;   // after the last thermostat request
constexpr unsigned long POLL_WINDOW_MS  = 600;   // beyond this time the turn is skipped
// With the hot water setpoint forced, it is rewritten to the boiler at this rate
constexpr unsigned long DHW_REWRITE_MS  = 20000;
// Print every relayed frame on the serial port (useful for debugging)
constexpr bool          LOG_FRAMES      = false;
