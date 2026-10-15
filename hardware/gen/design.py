"""OpenTherm gateway board: component list and netlist.

The OpenTherm interface (master + slave, +24 V boost, optocouplers) is derived from
"Arduino OpenTherm Shield" by MPro (Michal Protasowicki), CERN-OHL-P-2.0, itself based on the
OpenTherm Gateway project (otgw.tclcode.com). Changes in this board:
  * Arduino shield header replaced by an ESP32-C3-WROOM-02 module, 3.3 V LDO and USB-C;
  * logic supply is 3.3 V (VCC of the original), the boost converter is fed from +5 V;
  * the optocoupler U2 LED is driven low-side (anode to +3V3, cathode to the GPIO), so that the
    GPIO polarity of the line towards the boiler matches Ihor Melnyk's OpenTherm library
    (idle = HIGH output);
  * outdoor NTC input, user LED, UART test pads, TVS on both bus connections, +5 V screw terminal.
"""

R = "Resistor_SMD:R_0805_2012Metric"
C = "Capacitor_SMD:C_0805_2012Metric"
D = "Diode_SMD:D_SOD-123"
LED = "LED_SMD:LED_0805_2012Metric"
SOT = "Package_TO_SOT_SMD:SOT-23"
TP = "TestPoint:TestPoint_Pad_D1.5mm"
TB = "TerminalBlock_4Ucon:TerminalBlock_4Ucon_1x02_P3.50mm_Horizontal"

# ref: (value, footprint, LCSC / note, description)
PARTS = {
    # --- boost 24 V ---------------------------------------------------------------
    "U1": ("MT3540-F23", "Package_TO_SOT_SMD:SOT-23-5", "C181783", "Boost converter (alt. RY3750 C370888)"),
    "L1": ("10uH", "Inductor_SMD:L_Bourns-SRN4018", "C177242", "Bourns SRN4018 10uH"),
    "D1": ("1N5819W", D, "C8598", "Schottky"),
    "R1": ("240k", R, "", "Feedback"),
    "R2": ("13k", R, "", "Feedback"),
    "C1": ("10u/16V", C, "", "Boost input"),
    "C2": ("22p", C, "", "Feedback"),
    "C3": ("100n/50V", C, "", "+24V"),
    "C4": ("10u/50V", C, "C440198", "+24V"),
    # --- boiler side interface ---------------------------------------------------
    "J1": ("BOILER", TB, "", "Screw terminal 3.5 mm pitch"),
    "D2": ("1N4148W", D, "C7420318", "Bridge"),
    "D3": ("1N4148W", D, "C7420318", "Bridge"),
    "D4": ("1N4148W", D, "C7420318", "Bridge"),
    "D5": ("1N4148W", D, "C7420318", "Bridge"),
    "D6": ("4V7", D, "C19077400", "Zener 4.7 V"),
    "D7": ("13V", D, "C22399883", "Zener 13 V"),
    "D8": ("4V3", D, "C19077399", "Zener 4.3 V"),
    "Q1": ("MMBT2907A", SOT, "C7420352", "PNP"),
    "R3": ("330", R, "", ""),
    "R4": ("220", R, "", ""),
    "R5": ("100", R, "", ""),
    "R6": ("560", R, "", "U2 LED resistor (to +3V3)"),
    "R7": ("10k", R, "", ""),
    "U2": ("LTV-817S", "Package_DIP:SMDIP-4_W9.53mm", "C109227", "Optocoupler: line towards the boiler"),
    "U3": ("LTV-817S", "Package_DIP:SMDIP-4_W9.53mm", "C109227", "Optocoupler: line from the boiler"),
    "D16": ("SMAJ28CA", "Diode_SMD:D_SMA", "", "TVS across BOILER terminal"),
    # --- thermostat side interface ----------------------------------------------
    "J2": ("THERM", TB, "", "Screw terminal 3.5 mm pitch"),
    "Q2": ("MMBT2907A", SOT, "C7420352", "PNP"),
    "Q3": ("MMBT2907A", SOT, "C7420352", "PNP"),
    "Q4": ("MMBT2907A", SOT, "C7420352", "PNP"),
    "Q5": ("MMBT2222A", SOT, "C8512", "NPN"),
    "Q6": ("MMBT2222A", SOT, "C8512", "NPN"),
    "D9": ("8V2", D, "C22399882", "Zener 8.2 V"),
    "R8": ("100", R, "", ""),
    "R9": ("33k", R, "", ""),
    "R10": ("20", R, "", ""),
    "R11": ("20", R, "", ""),
    "R12": ("10k", R, "", ""),
    "R13": ("150k", R, "", ""),
    "R14": ("1k", R, "", ""),
    "R15": ("4k7", R, "", ""),
    "D17": ("SMAJ28CA", "Diode_SMD:D_SMA", "", "TVS across THERM terminal"),
    # --- status LEDs ----------------------------------------------------------------
    "D10": ("PWR", LED, "C84256", "Power LED"),
    "D11": ("RxB", LED, "C2297", "Receive from boiler"),
    "D12": ("RxT", LED, "C2296", "Receive from thermostat"),
    "R16": ("680", R, "", "PWR LED (3.3 V)"),
    "R17": ("330", R, "", "RxB LED (3.3 V, green)"),
    "R18": ("1k", R, "", "RxT LED (3.3 V)"),
    # --- MCU --------------------------------------------------------------------------
    "U4": ("ESP32-C3-WROOM-02-N4", "RF_Module:ESP32-C3-WROOM-02", "C2934560", "ESP32-C3 module, 4 MB"),
    "C6": ("100n", C, "", "ESP32 supply decoupling"),
    "C8": ("1u", C, "", "EN delay"),
    "R22": ("10k", R, "", "EN pull-up"),
    "R23": ("10k", R, "", "IO9 (BOOT) pull-up"),
    "R24": ("10k", R, "", "IO2 pull-up (strapping)"),
    "R25": ("10k", R, "", "IO8 pull-up (strapping)"),
    "SW1": ("RESET", "Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2", "C72443", "Reset button"),
    "SW2": ("BOOT", "Button_Switch_SMD:SW_Push_1P1T_NO_CK_KMR2", "C72443", "Boot button"),
    "D15": ("USER", LED, "", "User LED (IO10)"),
    "R26": ("1k", R, "", "User LED resistor"),
    # --- power ----------------------------------------------------------------------
    "J3": ("USB-C", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12", "C165948", "USB-C 16 pin, power + native USB"),
    "J5": ("5V IN", TB, "", "Alternative 5 V supply"),
    "U5": ("AP2112K-3.3", "Package_TO_SOT_SMD:SOT-23-5", "C51118", "3.3 V LDO 600 mA"),
    "U6": ("USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", "C7519", "USB ESD protection"),
    "D13": ("1N5819W", D, "C8598", "USB VBUS -> +5V"),
    "D14": ("1N5819W", D, "C8598", "5V terminal -> +5V"),
    "C5": ("10u/16V", C, "", "3.3 V output"),
    "C9": ("1u", C, "", "LDO input"),
    "R19": ("5k1", R, "", "USB CC1"),
    "R20": ("5k1", R, "", "USB CC2"),
    # --- outdoor probe --------------------------------------------------------------
    "J6": ("NTC", TB, "", "Outdoor NTC 10k probe"),
    "R21": ("10k 0.1%", R, "", "NTC divider reference"),
    "C7": ("100n", C, "", "NTC filter"),
    # --- test points ----------------------------------------------------------------
    "TP1": ("+3V3", TP, "", ""),
    "TP2": ("GND", TP, "", ""),
    "TP3": ("+24V", TP, "", ""),
    "TP4": ("T+", TP, "", ""),
    "TP5": ("B1", TP, "", ""),
    "TP6": ("B2", TP, "", ""),
    "TP7": ("+5V", TP, "", ""),
    "TP8": ("TXD", TP, "", "UART0 TX (debug)"),
    "TP9": ("RXD", TP, "", "UART0 RX (debug)"),
    "H1": ("M3", "MountingHole:MountingHole_3.2mm_M3", "", ""),
    "H2": ("M3", "MountingHole:MountingHole_3.2mm_M3", "", ""),
}

# LCSC / JLCPCB part numbers for the parts above that do not carry one (value -> code).
# 0805, 1 % thick film (basic parts where available), X7R/X5R/C0G MLCC.
_LCSC = {
    "R": {"10k": "C17414", "1k": "C17513", "100": "C17408", "20": "C17544", "330": "C17630",
          "220": "C17557", "560": "C28636", "680": "C17798", "4k7": "C17673", "5k1": "C27834",
          "13k": "C17455", "33k": "C17633", "150k": "C17470", "240k": "C25824",
          "10k 0.1%": "C110775"},
    "C": {"10u/16V": "C1713", "22p": "C1804", "100n/50V": "C49678", "100n": "C49678",
          "1u": "C28323"},
}
_OTHER = {"D15": "C84256", "D16": "C113964", "D17": "C113964",
          "J1": "C474892", "J2": "C474892", "J5": "C474892", "J6": "C474892"}
for _ref, (_val, _fp, _lcsc, _desc) in list(PARTS.items()):
    if not _lcsc:
        _code = _OTHER.get(_ref) or _LCSC.get(_ref[0], {}).get(_val, "")
        if _code:
            PARTS[_ref] = (_val, _fp, _code, _desc)

# net -> list of "REF.PAD"
NETS = {
    "GND": ["C1.2", "C3.2", "C4.2", "C5.2", "C6.2", "C7.2", "C8.2", "C9.2", "D10.1", "D11.1", "D12.1", "D15.1",
            "J2.2", "J5.2", "J6.2", "R13.2", "R14.2", "R2.2", "R7.2", "R9.2", "R19.2", "R20.2",
            "SW1.2", "SW2.2", "TP2.1", "U1.2", "U5.2", "U6.2", "U4.9", "U4.19", "D17.2",
            "J3.A1", "J3.B1", "J3.A12", "J3.B12", "J3.SH"],
    "+5V": ["C1.1", "C9.1", "D13.1", "D14.1", "L1.1", "U1.4", "U1.5", "U5.1", "U5.3", "TP7.1"],
    "+3V3": ["C5.1", "C6.1", "Q5.1", "Q6.3", "R16.2", "R6.1", "R21.1", "R22.1", "R23.1", "R24.1", "R25.1",
             "TP1.1", "U3.4", "U4.1", "U5.5"],
    "+24V": ["C2.1", "C3.1", "C4.1", "D1.1", "Q2.2", "Q4.2", "R1.1", "R8.1", "TP3.1"],
    "VBUS": ["D13.2", "J3.A4", "J3.A9", "J3.B4", "J3.B9", "U6.5"],
    "5V_IN": ["D14.2", "J5.1"],
    "USB_DP": ["J3.A6", "J3.B6", "U6.3", "U6.4", "U4.14"],
    "USB_DM": ["J3.A7", "J3.B7", "U6.1", "U6.6", "U4.13"],
    "CC1": ["J3.A5", "R19.1"],
    "CC2": ["J3.B5", "R20.1"],
    # OpenTherm, thermostat side
    "MASTER_IN": ["Q6.2", "R14.1", "R18.2", "U4.5"],
    "MASTER_OUT": ["R15.1", "U4.6"],
    "TPLUS": ["D9.1", "J2.1", "Q3.3", "TP4.1", "D17.1"],
    "N_D9A": ["D9.2", "R12.2"],
    "Q2B": ["Q2.1", "Q3.2", "R10.2", "R8.2"],
    "Q2C": ["Q2.3", "Q3.1", "R9.1"],
    "Q4B": ["Q4.1", "Q5.3"],
    "Q4C": ["Q4.3", "R11.1"],
    "Q5E": ["Q5.2", "R15.2"],
    "Q6B": ["Q6.1", "R12.1", "R13.1"],
    "R10R11": ["R10.1", "R11.2"],
    # OpenTherm, boiler side
    "B1": ["D2.2", "D5.1", "J1.1", "TP5.1", "D16.1"],
    "B2": ["D3.2", "D4.1", "J1.2", "TP6.1", "D16.2"],
    "N_D2K": ["D2.1", "D3.1", "Q1.2", "R3.2"],
    "OTRTN": ["D4.2", "D5.2", "D6.2", "D7.2", "D8.2", "R5.2", "U3.2"],
    "N_D6K": ["D6.1", "Q1.3", "R4.1"],
    "N_D7K": ["D7.1", "Q1.1", "R3.1", "U2.4"],
    "N_D8K": ["D8.1", "U2.3"],
    "N_R4R5": ["R4.2", "R5.1", "U3.1"],
    "N_R6U2": ["R6.2", "U2.1"],
    "SLAVE_IN": ["R17.2", "R7.1", "U3.3", "U4.3"],
    "SLAVE_OUT": ["U2.2", "U4.4"],
    # boost
    "SW": ["D1.2", "L1.2", "U1.1"],
    "FB": ["C2.2", "R1.2", "R2.1", "U1.3"],
    # LEDs
    "N_LED_PWR": ["D10.2", "R16.1"],
    "N_LED_RXB": ["D11.2", "R17.1"],
    "N_LED_RXT": ["D12.2", "R18.1"],
    "N_LED_USER": ["D15.2", "R26.2"],
    "LED_USER": ["R26.1", "U4.10"],
    # MCU
    "EN": ["U4.2", "R22.2", "C8.1", "SW1.1"],
    "BOOT": ["U4.8", "R23.2", "SW2.1"],
    "IO2": ["U4.16", "R24.2"],
    "IO8": ["U4.7", "R25.2"],
    "TXD": ["U4.12", "TP8.1"],
    "RXD": ["U4.11", "TP9.1"],
    "NTC_ADC": ["U4.15", "R21.2", "C7.1", "J6.1"],
}

# GPIO map of the module (documentation / firmware)
GPIO = {
    "SLAVE_IN (from boiler)": "IO4",
    "SLAVE_OUT (to boiler)": "IO5",
    "MASTER_IN (from thermostat)": "IO6",
    "MASTER_OUT (to thermostat)": "IO7",
    "NTC": "IO3",
    "USER LED": "IO10",
    "USB D-/D+": "IO18/IO19",
    "BOOT": "IO9",
}

if __name__ == "__main__":
    # consistency check: every pad used once, every part used
    used = {}
    for net, pins in NETS.items():
        for p in pins:
            if p in used:
                print("PIN IN DUE NET:", p, used[p], net)
            used[p] = net
    refs = {p.split(".")[0] for p in used}
    print("parts without connections:", sorted(set(PARTS) - refs))
    print("references in nets but not in parts:", sorted(refs - set(PARTS)))
    print(len(PARTS), "parts,", len(NETS), "nets,", len(used), "connections")
