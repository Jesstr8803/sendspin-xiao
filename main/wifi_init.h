#pragma once

#include "esp_err.h"

esp_err_t wifi_start_and_wait(const char* ssid, const char* password, uint32_t timeout_ms);
bool wifi_is_connected();

// Initialize the WiFi/netif/event subsystem WITHOUT starting STA mode.
// Used when we want to enter provisioning (SoftAP) mode instead.
esp_err_t wifi_init_only();
