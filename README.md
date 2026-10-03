# OpenTherm ⇄ SmartThings gateway

A DIY **OpenTherm** gateway based on the ESP32-C3. It sits between your room thermostat and your boiler,
relays everything transparently, and lets you read and control the boiler from **SmartThings** through a
native **Edge driver** (no Matter, no Home Assistant).

```
thermostat ── T1/T2 [Slave shield]  ESP32-C3  [Master shield] B1/B2 ── boiler
                                       │ Wi-Fi, HTTP/JSON
                        SmartThings hub (Edge driver)  ──  SmartThings app
```

**Background:** I (the author) switched to Home Assistant two years ago and I am very happy with it. This project
was built for my father's boiler, in a household that still runs SmartThings. It is shared in the hope that it is
useful to other SmartThings users with an OpenTherm boiler. The HTTP/JSON API below can also be used from any
other home automation system.

---

## ⚠️ Important warnings

- **Experimental project, provided "as is", with no warranty of any kind** (see [LICENSE](LICENSE)).
- It involves **a gas appliance**. The gateway is wired **in series** between thermostat and boiler: if the
  ESP loses power or hangs, the thermostat can no longer talk to the boiler. Decide for yourself what
  happens in your installation in that case.
- The gateway can **switch heating off**, **cap the flow temperature**, and in "App command" mode it
  **replaces the thermostat**. It is not a safety device.
- Wiring on the boiler terminals must be done **with the power off** and, if you are not experienced,
  **by a qualified technician**. Never work on gas or 230 V parts.
- This project is not a product of any boiler manufacturer, DIYLess, Samsung or SmartThings.

---

## What it does

- **Relays transparently** every OpenTherm message between thermostat and boiler (it keeps working
  when Wi-Fi or SmartThings are unavailable).
- **Reads all the data that passes through** and asks the boiler for more in the thermostat's pauses:
  flow, return and hot water temperature, modulation, flame, faults, etc. (depending on what your boiler
  supports).
- **Controls from SmartThings:**
  - heating on/off (clears the "CH enable" bit in the status message);
  - domestic hot water on/off and setpoint;
  - flow temperature cap;
  - **App command**: the gateway regulates the room temperature towards a target set from SmartThings,
    using the temperature measured by the original thermostat;
  - **Auto flow**: the thermostat decides when to heat, the gateway computes the lowest possible flow
    temperature (useful with condensing boilers);
  - optional NTC outdoor probe: outdoor temperature, also provided to the thermostat.
- A local **HTTP/JSON API** for diagnostics and integration.

## Hardware

| Part | Notes |
|---|---|
| LOLIN (WEMOS) **C3 mini** | ESP32-C3, D1 mini form factor |
| **DIYLess Master OpenTherm shield** | boiler side (B1/B2) |
| **DIYLess Slave OpenTherm shield** | thermostat side (T1/T2) |
| Expansion shield (optional) | to mount the three boards side by side |
| 10 kΩ NTC probe (optional) | for the outdoor temperature, with a 10 kΩ resistor and a 100 nF capacitor |

Pins used (D1 mini position → C3 mini GPIO):

| Function | Position | GPIO |
|---|---|---|
| Master, OT input | D2 | 8 |
| Master, OT output | D1 | 10 |
| Slave, OT input | D6 | 0 |
| Slave, OT output | D7 | 4 |
| Outdoor probe (optional) | A0 | 3 |

They can be changed in [`src/config.h`](src/config.h). GPIO8 is a strapping pin of the ESP32-C3: if flashing
fails with the master shield mounted, remove the shield or hold BOOT.

**Outdoor probe (optional):** `3V3 ── R 10 kΩ ──┬── NTC ── GND`, with 100 nF between the node and GND and
the node connected to GPIO3. Enable it only after wiring it: `POST /api/settings {"outdoorEnabled":true}`.

## Firmware

1. Copy `src/secrets.example.h` to `src/secrets.h` and fill in your Wi-Fi (2.4 GHz). This file must never be
   committed (it is already in `.gitignore`).
2. `pio run -t upload` over USB the first time; later `pio run -e ota -t upload` for over-the-air updates
   (set `upload_port` in `platformio.ini`).
3. The serial monitor (115200) shows the IP, the relayed messages and the IDs requested by the thermostat.
4. Give the ESP a **fixed IP** in your router.

Every boiler supports different OpenTherm IDs: use `GET /api/probe?id=N` to test yours and adapt
`POLL_IDS` in `config.h`.

## SmartThings driver (Edge)

**Quick install (driver only):** open this invitation link with the account that owns your hub, enroll the hub and
install the driver "OpenTherm Gateway" from the channel:

<https://bestow-regional.api.smartthings.com/invite/a5z2RPnwDj1n>

The custom capabilities live in the author's SmartThings namespace and should be usable from any account,
but this has only been tested on the author's account. Feedback is welcome (open an issue).

To build and publish your own copy (recommended if you want to modify anything), follow the steps below.

You need the [SmartThings CLI](https://github.com/SmartThingsCommunity/smartthings-cli) (on the first command
a browser opens for sign-in) and a hub that supports Edge drivers.

1. **Custom capabilities** (`capabilities/` folder): modulation, maximum flow temperature, hot water setpoint,
   boiler status. For each one:
   ```
   smartthings capabilities:create -i capabilities/<name>.json
   smartthings capabilities:presentation:create <namespace>.<name> --capability-version 1 -i capabilities/presentations/<name>.json
   smartthings capabilities:translations:upsert <namespace>.<name> --capability-version 1 -i capabilities/translations/<name>.it.json
   ```
2. In `driver/profiles/opentherm-gateway.yml` and in `driver/src/init.lua` replace the namespace
   `bookmusic32648` with **your own** namespace (the one assigned by your first `capabilities:create`).
3. Create a private channel, publish the driver and install it on your hub:
   ```
   smartthings edge:channels:create          (type DRIVER)
   smartthings edge:channels:enroll <hub-id> --channel <channel-id>
   smartthings edge:drivers:package driver --channel <channel-id> --hub <hub-id>
   ```
4. In the SmartThings app: add device → scan nearby → "OpenTherm Gateway", then in the device settings enter
   the **IP address of the ESP**.

If you change the **structure** of the profile (components or capabilities moved), delete the device in the
app and add it again; otherwise the app may show connection errors.

> **Note on languages:** the user-facing labels of the SmartThings device (component names in the profile
> and the capability presentations) are in Italian, because that is the language of the original household.
> Everything else (code, comments, logs, API, documentation) is in English. To localize the UI, edit the
> `label` fields in `driver/profiles/opentherm-gateway.yml` and the labels in `capabilities/presentations/`
> and `capabilities/translations/`.

### Device in SmartThings

Component ids in the profile are Italian technical identifiers (`main` = house, `comandoapp` = app command,
`riscaldamento` = heating, `mandataauto` = auto flow, `sanitaria` = hot water, `caldaia` = boiler,
`ritorno` = return, `esterna` = outdoor).

| Component | Content |
|---|---|
| Casa (House) | room temperature (from the original thermostat), Off/Heat mode, target, state |
| Comando da app (App command) | on = the app regulates, off = the wall thermostat decides |
| Riscaldamento (Heating) | switch, maximum flow temperature |
| Mandata auto (Auto flow) | switch |
| Acqua calda (Hot water) | switch, setpoint, temperature |
| Caldaia (Boiler) | flow temperature, modulation, status (flame, heating, hot water, thermostat, fault) |
| Ritorno, Esterna (Return, Outdoor) | temperature |

## Statistics page

Open `http://otgw.local/` (or the IP of the ESP) in a browser on your local network. It shows, for the last
24 hours: flame-on time, heating and hot water time, burner starts, average modulation, the share of burner time
spent condensing (return below ~55 °C), and charts of temperatures (house, outdoor, flow, return, requested flow),
modulation/flame and heating/hot water duty. The page is Italian or English depending on the browser language.

The history is kept in RAM (about 23 KB, one point per minute) and is **cleared when the ESP restarts**. Lifetime
burner hours and starts are saved to flash every 30 minutes. Heavy pages are limited to one request every 2 seconds,
because on a single core a flood of Wi-Fi traffic can disturb the OpenTherm timing.

## API

- `GET /api/state` — full state as JSON (`null` = value not available)
- `POST /api/settings` — JSON with one or more of: `chEnable`, `dhwEnable`, `chMax`, `dhwSetpoint`,
  `roomControl`, `roomTarget`, `flowAuto`, `outdoorEnabled`
- `GET /api/ids` — OpenTherm IDs requested by the thermostat and rejected by the boiler
- `GET /api/probe?id=N` — test read of one ID (read-only)
- `GET /` — statistics page; `GET /api/stats` — 24 h summary; `GET /api/history?step=N` — 24 h history in points of N minutes (N ≥ 5)

If `API_TOKEN` in `secrets.h` is not empty, POST requests need the `X-Token` header. There is no encryption:
use it only on a trusted local network.

## Regulation (to be tuned)

The room temperature regulator (hysteresis + PI) and "Auto flow" use the `CTRL_*` parameters in
[`src/config.h`](src/config.h). The starting values are conservative but **theoretical**: they must be tuned by
watching how your house behaves. If your boiler does not provide the outdoor temperature, the flow ceiling is
fixed; with the outdoor probe it follows a heating curve.

## Known limitations

- The gateway has only been tested on a single installation.
- The API is not encrypted.
- With the gateway in series, an ESP failure interrupts the thermostat–boiler dialogue.
- The custom capabilities belong to a namespace: use your own if you want to modify them.

## Credits and license

- [OpenTherm Library](https://github.com/ihormelnyk/opentherm_library) by Ihor Melnyk (MIT).
- OpenTherm shields by [DIYLess](https://diyless.com).
- [ArduinoJson](https://arduinojson.org) by Benoit Blanchon (MIT).
- License of this project: **MIT** (see [LICENSE](LICENSE)).
