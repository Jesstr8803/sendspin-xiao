#pragma once

#include <atomic>

#include "driver/i2s_std.h"
#include "driver/i2s_types.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sendspin/client.h"
#include "sendspin/player_role.h"

class NvsPersistence;

class I2sAudioSink : public sendspin::PlayerRoleListener {
public:
    I2sAudioSink(gpio_num_t lrck, gpio_num_t bck, gpio_num_t dout, gpio_num_t xsmt,
                 sendspin::PlayerRole& player, sendspin::SendspinClient* client = nullptr,
                 NvsPersistence* nvs = nullptr);
    ~I2sAudioSink();

    esp_err_t init();

    size_t on_audio_write(uint8_t* data, size_t length, uint32_t timeout_ms) override;
    void on_stream_start() override;
    void on_stream_end() override;
    void on_stream_clear() override;
    void on_volume_changed(uint8_t volume) override;
    void on_mute_changed(bool muted) override;

    static bool IRAM_ATTR i2s_on_sent_isr(i2s_chan_handle_t handle,
                                          i2s_event_data_t* event,
                                          void* user_ctx);
    static void notify_task(void* arg);

private:
    esp_err_t reconfigure(uint32_t sample_rate, uint8_t channels, uint8_t bit_depth);

    void set_xsmt(bool unmuted);

    gpio_num_t lrck_gpio_;
    gpio_num_t bck_gpio_;
    gpio_num_t dout_gpio_;
    gpio_num_t xsmt_gpio_;
    sendspin::PlayerRole& player_;
    sendspin::SendspinClient* client_;
    NvsPersistence* nvs_;
    i2s_chan_handle_t tx_handle_{nullptr};
    bool channel_enabled_{false};

    uint32_t current_rate_{0};
    uint8_t current_channels_{0};
    uint8_t current_bit_depth_{0};

    std::atomic<int32_t> frames_buffered_{0};
    QueueHandle_t sent_event_queue_{nullptr};
    std::atomic<int64_t> last_audio_us_{0};

    static void xsmt_idle_task(void* arg);

    volatile uint8_t volume_{75};
    volatile bool muted_{false};
};
