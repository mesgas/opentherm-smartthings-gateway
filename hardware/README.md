# Gateway PCB

A single compact board for the OpenTherm gateway: it replaces the LOLIN C3 mini + the two DIYLess
master/slave shields. It sits between the boiler and the original thermostat and runs the same
firmware as the breadboard version (`src/`, environment `gateway_pcb`).

> **Status: designed, DRC/ERC clean, not yet fabricated.** The OpenTherm front end is taken from an
> existing, working open design (see *Origin*), but the changes listed below are untested until the
> first board is built. Follow the bring-up sequence before connecting the boiler.

| | |
|---|---|
| Size | 56 x 42 mm, 4 layers (F / GND / GND+routing / B), parts on both sides |
| MCU | ESP32-C3-WROOM-02-N4 (Wi-Fi 2.4 GHz, antenna toward the top edge, nothing under it) |
| Power | USB-C (also native USB for flashing) **or** 5 V screw terminal J5; the two are OR-ed with Schottky diodes |
| Bus | J1 `BOILER` and J2 `THERM` (OpenTherm, polarity-free), 28 V TVS on both |
| Extra | J6 outdoor NTC 10 k (to 3V3 through a 10 k 0.1 % resistor), RESET / BOOT buttons, 3 status LEDs + 1 user LED |
| Mounting | 2 x M3 holes on a diagonal, 3.2 mm |

## Files

| Path | What |
|---|---|
| `opentherm-gateway.kicad_sch` / `.kicad_pcb` / `.kicad_pro` | KiCad 10 project |
| `production/opentherm-gateway-gerber.zip` | Gerbers + drill, ready for a PCB house |
| `production/bom.csv` | Bill of materials with LCSC part numbers (`bom-jlcpcb.csv` is the same in JLCPCB assembly format) |
| `production/positions.csv` | Pick-and-place, both sides (for reference; assembly is by hand) |
| `gen/` | Python generators: `design.py` is the single source of truth (parts + netlist), `gen_sch.py`, `build_pcb.py`, `route.py` (Freerouting), `export.py` |

The schematic is generated from the same netlist as the board (each pin carries a net label), so the
two cannot disagree. To change the design edit `gen/design.py` / `gen/placement.py` and re-run the
scripts (KiCad 10 python, Java for Freerouting).

## Origin and changes

The OpenTherm interface (opto-isolated master + slave, +24 V boost, current-sense circuits) is
derived from the *Arduino OpenTherm Shield* by MPro (Michal Protasowicki, CERN-OHL-P-2.0,
<https://github.com/michpro/Arduino_OpenTherm_shield>), which is itself based on the OpenTherm
Gateway of Schelte Bron (<https://otgw.tclcode.com>). This board keeps that circuit and changes:

* the Arduino header is replaced by the ESP32-C3 module, a 3.3 V LDO (AP2112K) and USB-C;
* logic runs at 3.3 V, the boost converter is fed from 5 V;
* the LED of the optocoupler towards the boiler (U2) is driven low-side from the GPIO, so that the
  line polarity matches the OpenTherm library used by the firmware (idle = HIGH output);
* added: TVS on both bus terminals, outdoor NTC input, user LED, UART test pads (TP8/TP9).

Released under the same license, CERN-OHL-P-2.0.

## GPIO map (ESP32-C3)

| GPIO | Net | Function |
|---|---|---|
| IO4 | SLAVE_IN | from the boiler (opto U3) = `PIN_M_IN` in firmware |
| IO5 | SLAVE_OUT | towards the boiler (opto U2) = `PIN_M_OUT` |
| IO6 | MASTER_IN | from the thermostat = `PIN_S_IN` |
| IO7 | MASTER_OUT | towards the thermostat = `PIN_S_OUT` |
| IO3 | NTC_ADC | outdoor probe (J6) |
| IO10 | LED_USER | user LED (not used by the firmware yet) |
| IO18 / IO19 | USB D- / D+ | native USB (USB-C) |
| IO9 | BOOT | boot button (hold while pressing RESET to enter the ROM bootloader) |
| IO2 / IO8 | | strapping pins, pulled up |
| IO20 / IO21 | RXD / TXD | UART0 test pads TP9 / TP8 |

## Bring-up (do this in order, boiler disconnected)

1. Visual check, then measure resistance between `GND` and `+5V`, `+3V3`, `+24V` (no shorts).
2. Power from USB only. `+5V` ~ 4.7 V (one diode drop), `+3V3` = 3.3 V (TP1), `+24V` ~ 24 V (TP3).
   If `+24V` is wrong check R1 (240 k) / R2 (13 k): Vout = 1.25 V x (1 + R1/R2).
3. Flash `gateway_pcb` over USB (`pio run -e gateway_pcb -t upload`; if the port does not appear,
   hold BOOT and tap RESET).
4. With nothing on J1/J2 the PWR LED is on and RxB/RxT are off. Connect only the thermostat to J2:
   measure `T+` (TP4) and check that the RxT LED blinks on each thermostat message.
5. Connect the boiler to J1 last. The gateway must relay: check `/api/status` and the LEDs.
6. If the boiler status stays INVALID while the thermostat talks, the polarity of the line towards
   the boiler is the first thing to check (see the note on U2 above).

## Ordering notes

* 4 layers, 1.6 mm, FR-4, HASL is fine. Min track/space used: 0.15 / 0.2 mm, min drill 0.3 mm.
* Hand assembly: all passives are 0805, the ESP32 module has its pads on the underside (hot air and
  flux). Parts are on both sides; do the side with the module first.
* The BOM has an LCSC code for every part (checked against the JLCPCB parts library, all in stock when
  checked). Equivalent parts can be substituted; the screw terminals (KF350-3.5-2P) and the inductor
  should be compared with their datasheets against the footprints before ordering.
* RxB is a green LED (R17 = 330 ohm), PWR red, RxT yellow, USER red.
