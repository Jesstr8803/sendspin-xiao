#pragma once

#include "esp_err.h"

esp_err_t ota_server_start(uint16_t port, const char* bearer_token);
