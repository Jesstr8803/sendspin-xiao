#include "nvs_persistence.h"

#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "nvs_persist";
static const char* NS = "sendspin";

namespace {

bool set_u32(const char* key, uint32_t value) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_u32(h, key, value) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    return ok;
}

std::optional<uint32_t> get_u32(const char* key) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return std::nullopt;
    uint32_t value = 0;
    esp_err_t err = nvs_get_u32(h, key, &value);
    nvs_close(h);
    if (err != ESP_OK) return std::nullopt;
    return value;
}

bool set_u16(const char* key, uint16_t value) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_u16(h, key, value) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    return ok;
}

std::optional<uint16_t> get_u16(const char* key) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return std::nullopt;
    uint16_t value = 0;
    esp_err_t err = nvs_get_u16(h, key, &value);
    nvs_close(h);
    if (err != ESP_OK) return std::nullopt;
    return value;
}

bool set_u8(const char* key, uint8_t value) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = (nvs_set_u8(h, key, value) == ESP_OK) && (nvs_commit(h) == ESP_OK);
    nvs_close(h);
    return ok;
}

std::optional<uint8_t> get_u8(const char* key) {
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return std::nullopt;
    uint8_t value = 0;
    esp_err_t err = nvs_get_u8(h, key, &value);
    nvs_close(h);
    if (err != ESP_OK) return std::nullopt;
    return value;
}

}  // namespace

bool NvsPersistence::save_last_server_hash(uint32_t hash) {
    return set_u32("last_srv", hash);
}

std::optional<uint32_t> NvsPersistence::load_last_server_hash() {
    return get_u32("last_srv");
}

bool NvsPersistence::save_static_delay(uint16_t delay_ms) {
    return set_u16("static_dly", delay_ms);
}

std::optional<uint16_t> NvsPersistence::load_static_delay() {
    return get_u16("static_dly");
}

bool NvsPersistence::save_volume(uint8_t volume) {
    return set_u8("volume", volume);
}

std::optional<uint8_t> NvsPersistence::load_volume() {
    return get_u8("volume");
}

bool NvsPersistence::save_muted(bool muted) {
    return set_u8("muted", muted ? 1 : 0);
}

std::optional<bool> NvsPersistence::load_muted() {
    auto v = get_u8("muted");
    if (!v) return std::nullopt;
    return *v != 0;
}
