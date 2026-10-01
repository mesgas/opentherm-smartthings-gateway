#pragma once

// ---------------------------------------------------------------------------
// Pin del LOLIN C3 mini (posizioni D1 mini delle shield DIYLess)
// ---------------------------------------------------------------------------
constexpr int PIN_M_IN  = 8;   // D2  master shield (verso caldaia): ingresso OT
constexpr int PIN_M_OUT = 10;  // D1  master shield: uscita OT
constexpr int PIN_S_IN  = 0;   // D6  slave shield (verso termostato): ingresso OT
constexpr int PIN_S_OUT = 4;   // D7  slave shield: uscita OT

// ---------------------------------------------------------------------------
// Sonda esterna NTC (TEWA TT05-10KC8-1S-T105-1500: 10 kΩ a 25 °C, B25/85 = 3435 K)
// Partitore: 3V3 - R_FIXED - [nodo -> PIN_NTC] - NTC - GND, con 100 nF tra nodo e GND
// ---------------------------------------------------------------------------
constexpr int   PIN_NTC        = 3;       // posizione "A0" della D1 mini = GPIO3 (ADC1)
constexpr float NTC_R_FIXED    = 10000;   // resistenza fissa verso 3V3 (ohm), meglio 0,1-1 %
constexpr float NTC_R0         = 10000;   // resistenza della sonda a NTC_T0
constexpr float NTC_T0         = 25;
constexpr float NTC_B          = 3435;
constexpr float NTC_VCC_MV     = 3300;    // tensione reale del pin 3V3: misurala col tester e correggi qui
constexpr float NTC_OFFSET_C   = 0;       // correzione finale in °C (confronto con un termometro di riferimento)
constexpr unsigned long OUTDOOR_READ_MS = 10000;

// ---------------------------------------------------------------------------
// Rete
// ---------------------------------------------------------------------------
#define HOSTNAME "otgw"        // http://otgw.local/api/state
constexpr uint16_t HTTP_PORT = 80;

// ---------------------------------------------------------------------------
// Limiti dei setpoint: adattali alla tua caldaia (qui: riscaldamento 30-85 °C, sanitaria 38-60 °C)
// ---------------------------------------------------------------------------
constexpr float CH_MAX_MIN      = 30;   // limite mandata: minimo impostabile
constexpr float CH_MAX_NO_LIMIT = 85;   // a questo valore il limite e' disattivato
constexpr float DHW_SET_MIN     = 38;
constexpr float DHW_SET_MAX     = 60;

// ---------------------------------------------------------------------------
// Controllo della temperatura ambiente da SmartThings (modalita' "heat")
// Il gateway sostituisce il termostato: accende/spegne il riscaldamento con
// isteresi e calcola la mandata con un regolatore PI sulla temperatura ambiente
// (ID 24, che il termostato invia alla caldaia). Da tarare sull'impianto reale.
// ---------------------------------------------------------------------------
constexpr float ROOM_TARGET_MIN = 10;
constexpr float ROOM_TARGET_MAX = 30;
constexpr float CTRL_ON_BELOW   = 0.3;   // accende se ambiente < obiettivo - 0,3 °C
constexpr float CTRL_OFF_ABOVE  = 0.2;   // spegne se ambiente > obiettivo + 0,2 °C
constexpr float CTRL_FLOW_MIN   = 30;    // mandata minima quando il riscaldamento e' acceso
constexpr float CTRL_KP         = 10;    // °C di mandata per ogni °C di errore
constexpr float CTRL_KI         = 0.15;  // °C di mandata per ogni (°C di errore x minuto)
constexpr float CTRL_I_MAX      = 25;    // limite del termine integrale (°C di mandata)
// Efficienza (caldaia a condensazione): mandata bassa = ritorno sotto ~55 °C = condensazione
constexpr float CTRL_ECO_FLOW_MAX = 50;  // tetto "normale" della mandata calcolata
constexpr float CTRL_BOOST_ERR    = 1.0; // se la casa e' piu' fredda di cosi' per CTRL_BOOST_AFTER_MS, il tetto sale al limite
constexpr unsigned long CTRL_BOOST_AFTER_MS = 1800000;   // 30 minuti
constexpr float CTRL_RETURN_MAX   = 55;  // sopra questo ritorno la mandata viene ridotta (niente condensazione)
// Curva climatica (solo con la sonda esterna attiva): tetto = BASE + PENDENZA x (RIF - temperatura esterna)
// Con i valori sotto: 20 °C fuori = 30, 10 °C = 42, 0 °C = 54, -5 °C = 60. Da tarare sull'impianto.
constexpr float CTRL_CURVE_BASE   = 30;
constexpr float CTRL_CURVE_SLOPE  = 1.2;
constexpr float CTRL_CURVE_REF    = 20;
constexpr unsigned long CTRL_STEP_MS   = 30000;    // periodo del regolatore
constexpr unsigned long CTRL_WRITE_MS  = 15000;    // riscrive la mandata alla caldaia con questa cadenza
constexpr unsigned long ROOM_STALE_MS  = 600000;   // senza temperatura ambiente da 10 min: torna al termostato

// ---------------------------------------------------------------------------
// Gateway
// ---------------------------------------------------------------------------
// Richieste extra alla caldaia, inviate nella pausa tra un ciclo e l'altro del termostato.
// Elenco ricavato provando una caldaia a condensazione: 18, 19, 27 non supportati; 33 senza risposta.
// Ogni caldaia e' diversa: prova i tuoi ID con /api/probe?id=N e adatta l'elenco.
// ID: 25 mandata, 28 ritorno, 26 sanitaria, 17 modulazione, 5 guasti, 56 setpoint sanitaria
constexpr uint8_t       POLL_IDS[]      = {25, 28, 26, 17, 5, 56};
constexpr uint8_t       POLL_TIMEOUTS_MAX = 5;   // timeout consecutivi prima di escludere un ID
constexpr bool          POLL_ENABLED    = true;
// Se il termostato non parla da IDLE_AFTER_MS, il gateway interroga la caldaia da solo (un dato ogni IDLE_POLL_MS)
constexpr unsigned long IDLE_AFTER_MS   = 5000;
constexpr unsigned long IDLE_POLL_MS    = 2000;
constexpr unsigned long POLL_AFTER_MS   = 350;   // dopo l'ultima richiesta del termostato
constexpr unsigned long POLL_WINDOW_MS  = 600;   // oltre questo tempo salta il turno
// Con il setpoint sanitaria forzato lo riscrive alla caldaia con questa cadenza
constexpr unsigned long DHW_REWRITE_MS  = 20000;
// Stampa su seriale ogni frame inoltrato (utile per il debug)
constexpr bool          LOG_FRAMES      = false;
