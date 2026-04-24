#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_pthread.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "sendspin/client.h"
#include "sendspin/player_role.h"

#include "i2s_audio_sink.h"
#include "mdns_init.h"
#include "nvs_persistence.h"
#include "status_led.h"
#include "wifi_init.h"

using namespace sendspin;

static const char* TAG = "sendspin-xiao";

struct DeviceNetworkProvider : SendspinNetworkProvider {
    bool is_network_ready() override { return wifi_is_connected(); }
};

struct WifiPowerListener : SendspinClientListener {
    // Permanent PS_NONE set at WiFi init — ignore SDK power-mode hints.
    void on_request_high_performance() override {}
    void on_release_high_performance() override {}
    void on_time_sync_updated(float error_us) override {
        ESP_LOGD(TAG, "time sync error: %.1f us", error_us);
    }
};

static std::string build_client_id() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "xiao-%02x%02x%02x%02x%02x%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}

extern "C" void app_main() {
    ESP_LOGI(TAG, "booting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_LOGI(TAG, "connecting to WiFi '%s'", CONFIG_WIFI_SSID);
    if (wifi_start_and_wait(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWORD, 30000) != ESP_OK) {
        ESP_LOGE(TAG, "WiFi failed to connect, rebooting");
        esp_restart();
    }

    ESP_ERROR_CHECK(mdns_advertise_sendspin(CONFIG_DEVICE_NAME, CONFIG_SENDSPIN_PORT,
                                            CONFIG_SENDSPIN_PATH));

    SendspinClientConfig cfg;
    cfg.client_id = build_client_id();
    cfg.name = CONFIG_DEVICE_NAME;
    cfg.product_name = "XIAO ESP32-S3 + PCM5102A";
    cfg.manufacturer = "DIY";
    cfg.software_version = "0.1.0";
    cfg.httpd_psram_stack = true;
    cfg.time_burst_interval_ms = 2000;
    cfg.time_burst_size = 16;

    SendspinClient client(std::move(cfg));

    static NvsPersistence nvs_persistence;

    PlayerRoleConfig player_cfg;
    player_cfg.audio_formats = {
        {SendspinCodecFormat::FLAC, 2, 48000, 16},
        {SendspinCodecFormat::FLAC, 2, 44100, 16},
        {SendspinCodecFormat::OPUS, 2, 48000, 16},
        {SendspinCodecFormat::PCM, 2, 48000, 16},
        {SendspinCodecFormat::PCM, 2, 44100, 16},
    };
    player_cfg.audio_buffer_capacity = 4000000;
    player_cfg.psram_stack = false;
    player_cfg.priority = 10;
    auto& player = client.add_player(std::move(player_cfg));

    I2sAudioSink i2s_sink(
        static_cast<gpio_num_t>(CONFIG_I2S_LRCK_GPIO),
        static_cast<gpio_num_t>(CONFIG_I2S_BCK_GPIO),
        static_cast<gpio_num_t>(CONFIG_I2S_DOUT_GPIO),
        player, &client, &nvs_persistence);
    ESP_ERROR_CHECK(i2s_sink.init());

    DeviceNetworkProvider network_provider;
    WifiPowerListener client_listener;

    player.set_listener(&i2s_sink);
    client.set_network_provider(&network_provider);
    client.set_listener(&client_listener);
    client.set_persistence_provider(&nvs_persistence);

    auto saved_vol = nvs_persistence.load_volume();
    auto saved_muted = nvs_persistence.load_muted();
    player.update_volume(saved_vol.value_or(75));
    player.update_muted(saved_muted.value_or(false));

    // Steer SDK pthreads (sync task, WebSocket client task) to core 1.
    // Core 0 is busy with WiFi + lwIP; isolating audio on core 1 reduces jitter.
    esp_pthread_cfg_t pth_cfg = esp_pthread_get_default_config();
    pth_cfg.pin_to_core = 1;
    pth_cfg.prio = 10;
    pth_cfg.stack_size = 8192;
    esp_pthread_set_cfg(&pth_cfg);

    if (!client.start_server()) {
        ESP_LOGE(TAG, "start_server failed");
        esp_restart();
    }
    ESP_LOGI(TAG, "Sendspin server started on port %d path %s",
             CONFIG_SENDSPIN_PORT, CONFIG_SENDSPIN_PATH);

    status_led_start(client, CONFIG_STATUS_LED_GPIO, CONFIG_STATUS_LED_ACTIVE_LOW);

    while (true) {
        client.loop();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
