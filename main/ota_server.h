#pragma once

#include "esp_err.h"

class I2sAudioSink;

esp_err_t ota_server_start(uint16_t port, const char* bearer_token,
                           I2sAudioSink* sink = nullptr);

// Optional: register the audio sink for /status metrics after the OTA server
// has been started. Useful when the sink is constructed after OTA boot-up.
void ota_server_set_sink(I2sAudioSink* sink);
