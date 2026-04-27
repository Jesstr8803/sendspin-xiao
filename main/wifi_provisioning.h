#pragma once

#include "esp_err.h"

class NvsPersistence;

// Starts SoftAP + DNS hijack + HTTP captive portal so the user can pick a WiFi
// network from a phone. On successful save, credentials are written to NVS via
// the supplied persistence and the device reboots into STA mode.
// Returns ESP_OK once the AP is running. Blocks (in a separate task) until
// provisioning is complete (then triggers reboot).
esp_err_t wifi_provisioning_start(NvsPersistence& nvs, const char* ap_ssid_prefix);
