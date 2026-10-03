// Copy this file to secrets.h (git-ignored) and fill in the values.
#pragma once

#define WIFI_SSID     "your-2.4GHz-network"
#define WIFI_PASSWORD "your-wifi-password"

// If not empty, commands (POST) must carry the header "X-Token: <value>".
// The same value must be set in the device settings in SmartThings.
#define API_TOKEN     ""

// If not empty, it protects the over-the-air (OTA) update.
#define OTA_PASSWORD  ""

// Optional: name of the ESP on the local network (default "otgw" -> http://otgw.local/).
// #define HOSTNAME "my-boiler"
