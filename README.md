# OpenTherm ⇄ SmartThings gateway

Gateway **OpenTherm** fai-da-te basato su ESP32-C3 che si mette tra il termostato e la caldaia,
inoltra tutto in modo trasparente e permette di leggere e comandare l'impianto da **SmartThings**
tramite un driver **Edge** nativo (niente Matter, niente Home Assistant).

> **EN:** DIY OpenTherm gateway (ESP32-C3) that sits between your room thermostat and your boiler,
> forwards everything transparently and exposes the boiler to SmartThings through a native Edge
> driver. Italian is the main language of this README; an English summary is at the bottom.

```
termostato ── T1/T2 [Slave shield]  ESP32-C3  [Master shield] B1/B2 ── caldaia
                                       │ Wi-Fi, HTTP/JSON
                        hub SmartThings (driver Edge)  ──  app SmartThings
```

---

## ⚠️ Avvertenze importanti

- Progetto **sperimentale, fornito "così com'è", senza alcuna garanzia** (vedi [LICENSE](LICENSE)).
- Riguarda **un apparecchio a gas**. Il gateway sta **in serie** tra termostato e caldaia: se l'ESP
  perde corrente o si blocca, il termostato non comunica più con la caldaia. Valuta tu cosa succede
  nel tuo impianto in quel caso.
- Il gateway può **spegnere il riscaldamento** o **limitare la temperatura di mandata**, e in modalità
  "Comando da app" **sostituisce il termostato**. Non è un dispositivo di sicurezza.
- I collegamenti ai morsetti della caldaia vanno fatti **con la corrente staccata** e, se non sei
  pratico, **da un tecnico abilitato**. Non intervenire mai su parti a gas o a 230 V.
- Non è un prodotto di nessun costruttore di caldaie, di DIYLess, di Samsung o di SmartThings.

---

## Cosa fa

- **Inoltra in modo trasparente** ogni messaggio OpenTherm tra termostato e caldaia (funziona anche
  se il Wi-Fi o SmartThings non sono disponibili).
- **Legge tutti i dati che passano** e ne chiede altri alla caldaia nelle pause del termostato:
  temperatura di mandata, ritorno, acqua sanitaria, modulazione, fiamma, guasti, ecc.
  (dipende da cosa supporta la tua caldaia).
- **Comandi da SmartThings:**
  - riscaldamento acceso/spento (azzera il bit "CH enable" nel messaggio di stato);
  - acqua calda sanitaria acceso/spento e setpoint;
  - limite della temperatura di mandata;
  - **Comando da app**: il gateway regola la temperatura di casa su un obiettivo impostato da
    SmartThings, usando la temperatura misurata dal termostato originale;
  - **Mandata auto**: il termostato decide quando scaldare, il gateway calcola la mandata più bassa
    possibile (utile con caldaie a condensazione);
  - sonda esterna NTC opzionale: temperatura esterna, passata anche al termostato.
- **API HTTP/JSON** locale per diagnostica e integrazione.

## Hardware

| Pezzo | Note |
|---|---|
| LOLIN (WEMOS) **C3 mini** | ESP32-C3, formato D1 mini |
| **DIYLess Master OpenTherm shield** | lato caldaia (B1/B2) |
| **DIYLess Slave OpenTherm shield** | lato termostato (T1/T2) |
| Expansion shield (opzionale) | per montare le tre schede affiancate |
| Sonda NTC 10 kΩ (opzionale) | per la temperatura esterna, con una resistenza 10 kΩ e un condensatore 100 nF |

Pin usati (posizioni D1 mini → GPIO del C3 mini):

| Funzione | Posizione | GPIO |
|---|---|---|
| Master, ingresso OT | D2 | 8 |
| Master, uscita OT | D1 | 10 |
| Slave, ingresso OT | D6 | 0 |
| Slave, uscita OT | D7 | 4 |
| Sonda esterna (opzionale) | A0 | 3 |

Sono modificabili in [`src/config.h`](src/config.h). GPIO8 è un pin di strapping dell'ESP32-C3:
se il flash non parte con la master shield montata, toglila o tieni premuto BOOT.

**Sonda esterna (opzionale):** `3V3 ── R 10 kΩ ──┬── NTC ── GND`, con 100 nF tra il nodo e GND e il
nodo collegato a GPIO3. Attivala solo dopo averla cablata: `POST /api/settings {"outdoorEnabled":true}`.

## Firmware

1. Copia `src/secrets.example.h` in `src/secrets.h` e inserisci il Wi-Fi (2,4 GHz). Il file non va
   mai committato (è già nel `.gitignore`).
2. `pio run -t upload` via USB la prima volta; poi `pio run -e ota -t upload` per gli aggiornamenti via
   rete (imposta `upload_port` in `platformio.ini`).
3. Il monitor seriale (115200) mostra IP, messaggi inoltrati e gli ID richiesti dal termostato.
4. Assegna all'ESP un **IP fisso** nel router.

Ogni caldaia supporta ID OpenTherm diversi: usa `GET /api/probe?id=N` per provare la tua e adatta
`POLL_IDS` in `config.h`.

## Driver SmartThings (Edge)

Serve la [CLI di SmartThings](https://github.com/SmartThingsCommunity/smartthings-cli) (al primo
comando si apre il browser per l'accesso) e un hub compatibile con i driver Edge.

1. **Capability personalizzate** (cartella `capabilities/`): modulazione, temperatura massima mandata,
   setpoint acqua calda, stato caldaia. Per ognuna:
   ```
   smartthings capabilities:create -i capabilities/<nome>.json
   smartthings capabilities:presentation:create <namespace>.<nome> --capability-version 1 -i capabilities/presentations/<nome>.json
   smartthings capabilities:translations:upsert <namespace>.<nome> --capability-version 1 -i capabilities/translations/<nome>.it.json
   ```
2. In `driver/profiles/opentherm-gateway.yml` e in `driver/src/init.lua` sostituisci il namespace
   `bookmusic32648` con il **tuo** namespace (quello assegnato alla prima `capabilities:create`).
3. Crea un canale privato, pubblica il driver e installalo sul tuo hub:
   ```
   smartthings edge:channels:create          (type DRIVER)
   smartthings edge:channels:enroll <id-hub> --channel <id-canale>
   smartthings edge:drivers:package driver --channel <id-canale> --hub <id-hub>
   ```
4. Nell'app SmartThings: aggiungi dispositivo → cerca dispositivi nelle vicinanze → "Caldaia OpenTherm",
   poi nelle impostazioni del dispositivo scrivi l'**IP dell'ESP**.

Se cambi la **struttura** del profilo (componenti o capability spostati), cancella il dispositivo
dall'app e aggiungilo di nuovo: in caso contrario l'app può dare errori di connessione.

### Dispositivo in SmartThings

| Componente | Contenuto |
|---|---|
| Casa | temperatura ambiente (dal termostato originale), modalità Spento/Caldo, obiettivo, stato |
| Comando da app | acceso = regola l'app, spento = decide il termostato |
| Riscaldamento | interruttore, mandata massima |
| Mandata auto | interruttore |
| Acqua calda | interruttore, setpoint, temperatura |
| Caldaia | mandata, modulazione, stato (fiamma, riscaldamento, acqua, termostato, guasto) |
| Ritorno, Esterna | temperatura |

## API

- `GET /api/state` — stato completo in JSON (`null` = dato non disponibile)
- `POST /api/settings` — JSON con uno o più di: `chEnable`, `dhwEnable`, `chMax`, `dhwSetpoint`,
  `roomControl`, `roomTarget`, `flowAuto`, `outdoorEnabled`
- `GET /api/ids` — ID OpenTherm richiesti dal termostato e rifiutati dalla caldaia
- `GET /api/probe?id=N` — lettura di prova di un ID (solo lettura)

Se `API_TOKEN` in `secrets.h` non è vuoto, i POST richiedono l'header `X-Token`. Non c'è cifratura:
usalo solo su una rete locale fidata.

## Regolazione (da tarare)

Il regolatore della temperatura ambiente (isteresi + PI) e la "mandata auto" usano i parametri `CTRL_*`
di [`src/config.h`](src/config.h). I valori di partenza sono prudenti ma **teorici**: vanno tarati
osservando il comportamento della tua casa. Se la caldaia non fornisce la temperatura esterna, il
tetto della mandata è fisso; con la sonda esterna segue una curva climatica.

## Limiti noti

- Il gateway non è mai stato provato su più di un impianto.
- L'API non è cifrata.
- Con il gateway in serie, un guasto dell'ESP interrompe il dialogo termostato–caldaia.
- Le capability personalizzate appartengono a un namespace: usa le tue se vuoi modificarle.

## Crediti e licenza

- Libreria [OpenTherm Library](https://github.com/ihormelnyk/opentherm_library) di Ihor Melnyk (MIT).
- Shield OpenTherm di [DIYLess](https://diyless.com).
- [ArduinoJson](https://arduinojson.org) di Benoit Blanchon (MIT).
- Licenza di questo progetto: **MIT** (vedi [LICENSE](LICENSE)).

---

## English summary

A DIY OpenTherm gateway for the LOLIN C3 mini (ESP32-C3) with two DIYLess OpenTherm shields
(master towards the boiler, slave towards the thermostat). It transparently relays all OpenTherm
traffic, reads the boiler data and exposes it, with a few controls (heating on/off, DHW setpoint,
maximum flow temperature, app-controlled room temperature, efficient flow temperature), to SmartThings
through a native Edge driver. Setup: copy `src/secrets.example.h` to `src/secrets.h`, flash with
PlatformIO, create the custom capabilities with the SmartThings CLI (replace the namespace in the driver
profile and in `driver/src/init.lua`), publish the driver to a private channel and install it on your hub.

**Warning:** experimental, no warranty. It is connected in series between thermostat and boiler of a
gas appliance; wiring on the boiler terminals must be done with the power off and, if in doubt, by a
qualified technician. If the ESP loses power, the thermostat can no longer talk to the boiler.
