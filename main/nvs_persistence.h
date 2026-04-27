#pragma once

#include <optional>
#include <string>

#include "sendspin/client.h"

class NvsPersistence : public sendspin::SendspinPersistenceProvider {
public:
    bool save_last_server_hash(uint32_t hash) override;
    std::optional<uint32_t> load_last_server_hash() override;

    bool save_static_delay(uint16_t delay_ms) override;
    std::optional<uint16_t> load_static_delay() override;

    bool save_volume(uint8_t volume);
    std::optional<uint8_t> load_volume();

    bool save_muted(bool muted);
    std::optional<bool> load_muted();

    bool save_wifi_credentials(const char* ssid, const char* password);
    std::optional<std::string> load_wifi_ssid();
    std::optional<std::string> load_wifi_password();
    bool clear_wifi_credentials();

    bool save_device_name(const char* name);
    std::optional<std::string> load_device_name();
};
