#pragma once

#include "esp_err.h"

esp_err_t mdns_advertise_sendspin(const char* instance_name, uint16_t port, const char* path);
