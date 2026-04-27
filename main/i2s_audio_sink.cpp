#include "i2s_audio_sink.h"

#include <chrono>

#include <cstring>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_persistence.h"

static const char* TAG = "i2s_sink";

I2sAudioSink::I2sAudioSink(gpio_num_t lrck, gpio_num_t bck, gpio_num_t dout, gpio_num_t xsmt,
                            sendspin::PlayerRole& player,
                            sendspin::SendspinClient* client, NvsPersistence* nvs)
    : lrck_gpio_(lrck), bck_gpio_(bck), dout_gpio_(dout), xsmt_gpio_(xsmt),
      player_(player), client_(client), nvs_(nvs) {
    if (nvs_) {
        if (auto v = nvs_->load_volume()) volume_ = *v;
        if (auto m = nvs_->load_muted()) muted_ = *m;
    }
}

void I2sAudioSink::set_xsmt(bool unmuted) {
    if (xsmt_gpio_ == GPIO_NUM_NC) return;
    gpio_set_level(xsmt_gpio_, unmuted ? 1 : 0);
}

I2sAudioSink::~I2sAudioSink() {
    if (tx_handle_ != nullptr) {
        if (channel_enabled_) {
            i2s_channel_disable(tx_handle_);
        }
        i2s_del_channel(tx_handle_);
    }
}

struct SentEvent {
    int64_t timestamp_us;
    size_t bytes;
};

void I2sAudioSink::notify_task(void* arg) {
    auto* self = static_cast<I2sAudioSink*>(arg);
    SentEvent ev;
    for (;;) {
        if (xQueueReceive(self->sent_event_queue_, &ev, portMAX_DELAY) != pdTRUE) continue;
        uint32_t frame_bytes = self->current_channels_ * (self->current_bit_depth_ / 8);
        if (frame_bytes == 0) continue;
        int32_t buffer_frames = static_cast<int32_t>(ev.bytes / frame_bytes);
        int32_t avail = self->frames_buffered_.load(std::memory_order_relaxed);
        if (avail <= 0) continue;
        int32_t frames = buffer_frames < avail ? buffer_frames : avail;
        self->frames_buffered_.fetch_sub(frames, std::memory_order_relaxed);
        self->player_.notify_audio_played(static_cast<uint32_t>(frames), ev.timestamp_us);
    }
}

esp_err_t I2sAudioSink::init() {
    if (xsmt_gpio_ != GPIO_NUM_NC) {
        gpio_config_t cfg{};
        cfg.pin_bit_mask = 1ULL << xsmt_gpio_;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&cfg);
        // Start muted — PCM5102A's XSMT pulldown means it would mute on its own
        // until we explicitly drive high, but make it explicit so the state is
        // unambiguous in logs.
        set_xsmt(false);
        ESP_LOGI(TAG, "XSMT mute on GPIO %d (idle = mute, stream_start = unmute)",
                 (int)xsmt_gpio_);
    }
    sent_event_queue_ = xQueueCreate(16, sizeof(SentEvent));
    xTaskCreatePinnedToCore(&I2sAudioSink::notify_task, "notify_task", 4096, this, 8,
                            nullptr, 1);
    return reconfigure(48000, 2, 16);
}

esp_err_t I2sAudioSink::reconfigure(uint32_t sample_rate, uint8_t channels, uint8_t bit_depth) {
    if (tx_handle_ != nullptr) {
        if (sample_rate == current_rate_ && channels == current_channels_ &&
            bit_depth == current_bit_depth_) {
            return ESP_OK;
        }
        if (channel_enabled_) {
            i2s_channel_disable(tx_handle_);
            channel_enabled_ = false;
        }
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 1024;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_event_callbacks_t cbs = {};
    cbs.on_sent = &I2sAudioSink::i2s_on_sent_isr;
    ESP_ERROR_CHECK(i2s_channel_register_event_callback(tx_handle_, &cbs, this));

    i2s_data_bit_width_t bit_width = I2S_DATA_BIT_WIDTH_16BIT;
    if (bit_depth == 24) bit_width = I2S_DATA_BIT_WIDTH_24BIT;
    else if (bit_depth == 32) bit_width = I2S_DATA_BIT_WIDTH_32BIT;

    i2s_slot_mode_t slot_mode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bit_width, slot_mode),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bck_gpio_,
            .ws = lrck_gpio_,
            .dout = dout_gpio_,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    channel_enabled_ = true;

    current_rate_ = sample_rate;
    current_channels_ = channels;
    current_bit_depth_ = bit_depth;
    frames_buffered_.store(0, std::memory_order_relaxed);

    ESP_LOGI(TAG, "I2S reconfigured: %u Hz, %u ch, %u-bit", sample_rate, channels, bit_depth);
    return ESP_OK;
}

size_t I2sAudioSink::on_audio_write(uint8_t* data, size_t length, uint32_t timeout_ms) {
    static uint32_t call_count = 0;
    static uint64_t total_requested = 0;
    static uint64_t total_written = 0;

    if (tx_handle_ == nullptr || !channel_enabled_) {
        if ((call_count++ % 1000) == 0) {
            ESP_LOGW(TAG, "channel not ready (tx=%p enabled=%d)",
                     tx_handle_, channel_enabled_);
        }
        return 0;
    }

    const uint8_t* write_src = data;
    static uint8_t scratch[32768];

    // Kalman time filter not yet converged — emit silence so the pipeline
    // advances but no mistimed audio is audible.
    bool force_silence = (client_ != nullptr && !client_->is_time_synced()
                          && current_bit_depth_ == 16 && length <= sizeof(scratch));

    if (force_silence || (muted_ && current_bit_depth_ == 16 && length <= sizeof(scratch))) {
        std::memset(scratch, 0, length);
        write_src = scratch;
    } else if (current_bit_depth_ == 16 && length <= sizeof(scratch)) {
        uint8_t eff_vol = volume_ > 100 ? 100 : volume_;
        int32_t scale = static_cast<int32_t>(eff_vol) * eff_vol;
        if (scale < 10000) {
            const auto* src = reinterpret_cast<const int16_t*>(data);
            auto* dst = reinterpret_cast<int16_t*>(scratch);
            size_t n = length / 2;
            for (size_t i = 0; i < n; ++i) {
                dst[i] = static_cast<int16_t>(static_cast<int32_t>(src[i]) * scale / 10000);
            }
            write_src = scratch;
        }
    }

    static uint32_t dbg_count = 0;
    if ((dbg_count++ % 500) == 0) {
        ESP_LOGI(TAG, "write: len=%u bit_depth=%u vol=%u muted=%d scratch=%d",
                 (unsigned)length, current_bit_depth_, volume_, muted_,
                 write_src == scratch ? 1 : 0);
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(tx_handle_, write_src, length, &bytes_written,
                                      portMAX_DELAY);

    total_requested += length;
    total_written += bytes_written;
    if ((call_count++ % 1000) == 0) {
        ESP_LOGI(TAG, "write #%lu: req=%u got=%u err=%s (cumul %llu/%llu)",
                 call_count, (unsigned)length, (unsigned)bytes_written,
                 esp_err_to_name(err), total_written, total_requested);
    }

    if (err == ESP_OK || err == ESP_ERR_TIMEOUT) {
        uint32_t frame_bytes = current_channels_ * (current_bit_depth_ / 8);
        if (frame_bytes > 0 && bytes_written > 0) {
            frames_buffered_.fetch_add(bytes_written / frame_bytes, std::memory_order_relaxed);
        }
        return bytes_written;
    }
    ESP_LOGW(TAG, "i2s_channel_write error: %s", esp_err_to_name(err));
    return 0;
}

bool IRAM_ATTR I2sAudioSink::i2s_on_sent_isr(i2s_chan_handle_t handle,
                                              i2s_event_data_t* event,
                                              void* user_ctx) {
    auto* self = static_cast<I2sAudioSink*>(user_ctx);
    if (self == nullptr || event == nullptr || event->size == 0) return false;
    if (self->sent_event_queue_ == nullptr) return false;
    SentEvent ev{esp_timer_get_time(), event->size};
    BaseType_t need_yield = pdFALSE;
    xQueueSendToBackFromISR(self->sent_event_queue_, &ev, &need_yield);
    return need_yield == pdTRUE;
}

void I2sAudioSink::on_stream_start() {
    auto& params = player_.get_current_stream_params();
    if (!params.sample_rate.has_value() || !params.channels.has_value() ||
        !params.bit_depth.has_value()) {
        ESP_LOGW(TAG, "stream_start with incomplete params");
        return;
    }
    reconfigure(*params.sample_rate, *params.channels, *params.bit_depth);
    set_xsmt(true);
}

void I2sAudioSink::on_stream_end() {
    ESP_LOGI(TAG, "on_stream_end (leaving channel enabled)");
    // Don't mute here — on_stream_end fires between tracks in a queue and
    // muting would click. on_stream_clear is the actual "audio is done".
}

void I2sAudioSink::on_stream_clear() {
    ESP_LOGI(TAG, "on_stream_clear (flushing DMA)");
    set_xsmt(false);
    if (tx_handle_ && channel_enabled_) {
        i2s_channel_disable(tx_handle_);
        i2s_channel_enable(tx_handle_);
    }
}

void I2sAudioSink::on_volume_changed(uint8_t volume) {
    ESP_LOGI(TAG, "volume changed: %u", volume);
    volume_ = volume;
    if (nvs_) nvs_->save_volume(volume);
}

void I2sAudioSink::on_mute_changed(bool muted) {
    ESP_LOGI(TAG, "mute changed: %d", muted);
    muted_ = muted;
    if (nvs_) nvs_->save_muted(muted);
}
