// Copia questo file in secrets.h (escluso da git) e compila i valori.
#pragma once

#define WIFI_SSID     "nome-rete-2.4GHz"
#define WIFI_PASSWORD "password-wifi"

// Se non vuoto, i comandi (POST) devono avere l'header "X-Token: <valore>".
// Lo stesso valore va messo nelle impostazioni del dispositivo in SmartThings.
#define API_TOKEN     ""

// Se non vuota, protegge l'aggiornamento via rete (OTA).
#define OTA_PASSWORD  ""
