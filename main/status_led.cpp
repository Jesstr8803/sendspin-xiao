#include "status_led.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_init.h"

using namespace sendspin;

namespace {

struct LedCtx {
    SendspinClient* client;
    gpio_num_t gpio;
    bool active_low;
};

void led_set(const LedCtx& c, bool on) {
    gpio_set_level(c.gpio, (on ^ c.active_low) ? 1 : 0);
}

void led_task(void* arg) {
    auto* ctx = static_cast<LedCtx*>(arg);

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << ctx->gpio;
    io.mode = GPIO_MODE_OUTPUT;
    gpio_config(&io);
    led_set(*ctx, false);

    while (true) {
        bool wifi = wifi_is_connected();
        bool ma = wifi && ctx->client->is_connected();
        bool playing = false;
        if (ma) {
            auto& group = ctx->client->get_group_state();
            if (group.playback_state.has_value()) {
                playing = (*group.playback_state == SendspinPlaybackState::PLAYING);
            }
        }

        if (!wifi) {
            led_set(*ctx, true);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_set(*ctx, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else if (!ma) {
            led_set(*ctx, true);
            vTaskDelay(pdMS_TO_TICKS(200));
            led_set(*ctx, false);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else if (!playing) {
            led_set(*ctx, true);
            vTaskDelay(pdMS_TO_TICKS(100));
            led_set(*ctx, false);
            vTaskDelay(pdMS_TO_TICKS(2900));
        } else {
            led_set(*ctx, true);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

}  // namespace

void status_led_start(SendspinClient& client, int gpio, bool active_low) {
    if (gpio < 0) return;
    static LedCtx ctx{&client, static_cast<gpio_num_t>(gpio), active_low};
    xTaskCreate(led_task, "status_led", 2048, &ctx, 1, nullptr);
}
