#include "ota_server.h"

#include <cstring>
#include <string>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_persistence.h"

static const char* TAG = "ota_server";

namespace {

std::string g_token;
constexpr size_t OTA_BUF_SIZE = 4096;

bool token_matches(httpd_req_t* req) {
    if (g_token.empty()) return true;  // No token configured = no auth.
    size_t hdr_len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (hdr_len == 0 || hdr_len > 256) return false;
    char buf[260];
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    const char* prefix = "Bearer ";
    if (std::strncmp(buf, prefix, std::strlen(prefix)) != 0) return false;
    return g_token == (buf + std::strlen(prefix));
}

esp_err_t handle_ota_post(httpd_req_t* req) {
    if (!token_matches(req)) {
        ESP_LOGW(TAG, "OTA push rejected: bad/missing token");
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "bad token");
        return ESP_FAIL;
    }

    const esp_partition_t* update_part = esp_ota_get_next_update_partition(nullptr);
    if (update_part == nullptr) {
        ESP_LOGE(TAG, "no OTA partition available");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA push starting → partition '%s' (offset 0x%lx, size %lu)",
             update_part->label, (unsigned long)update_part->address,
             (unsigned long)update_part->size);

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin failed");
        return ESP_FAIL;
    }

    int total = req->content_len;
    int received = 0;
    char* buf = static_cast<char*>(malloc(OTA_BUF_SIZE));
    if (buf == nullptr) {
        esp_ota_abort(handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    bool magic_checked = false;
    while (received < total) {
        int chunk = httpd_req_recv(req, buf, OTA_BUF_SIZE);
        if (chunk <= 0) {
            if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "recv failed at %d/%d", received, total);
            esp_ota_abort(handle);
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            return ESP_FAIL;
        }
        if (!magic_checked) {
            if (static_cast<uint8_t>(buf[0]) != ESP_IMAGE_HEADER_MAGIC) {
                ESP_LOGE(TAG, "first byte 0x%02x is not an ESP image",
                         static_cast<uint8_t>(buf[0]));
                esp_ota_abort(handle);
                free(buf);
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "not an ESP image");
                return ESP_FAIL;
            }
            magic_checked = true;
        }
        err = esp_ota_write(handle, buf, chunk);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed at %d: %s", received, esp_err_to_name(err));
            esp_ota_abort(handle);
            free(buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_write failed");
            return ESP_FAIL;
        }
        received += chunk;
        if ((received & 0x1FFFF) == 0) {  // log every ~128KB
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes", received, total);
        }
    }
    free(buf);

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_end failed");
        return ESP_FAIL;
    }
    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set_boot failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA flash complete (%d bytes), rebooting in 1s", received);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK\n");

    // Defer restart so the response actually flushes
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_forget_wifi(httpd_req_t* req) {
    if (!token_matches(req)) {
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "bad token");
        return ESP_FAIL;
    }
    NvsPersistence nvs;
    nvs.clear_wifi_credentials();
    ESP_LOGI(TAG, "WiFi credentials cleared, rebooting into provisioning mode");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK, rebooting into provisioning AP\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

esp_err_t handle_status(httpd_req_t* req) {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_app_desc_t* app = esp_app_get_description();

    // RSSI is best-effort — fails if WiFi isn't yet up. Report 0 in that case.
    wifi_ap_record_t ap{};
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char body[512];
    snprintf(body, sizeof(body),
             "{"
             "\"version\":\"%s\","
             "\"build_date\":\"%s %s\","
             "\"idf_version\":\"%s\","
             "\"running_partition\":\"%s\","
             "\"address\":\"0x%lx\","
             "\"size\":%lu,"
             "\"uptime_s\":%lld,"
             "\"free_heap\":%lu,"
             "\"min_free_heap\":%lu,"
             "\"rssi\":%d,"
             "\"mac\":\"%02x:%02x:%02x:%02x:%02x:%02x\""
             "}\n",
             app ? app->version : "unknown",
             app ? app->date : "?", app ? app->time : "?",
             app ? app->idf_ver : "?",
             running ? running->label : "unknown",
             running ? (unsigned long)running->address : 0,
             running ? (unsigned long)running->size : 0,
             (long long)(esp_timer_get_time() / 1000000),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             rssi,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

}  // namespace

esp_err_t ota_server_start(uint16_t port, const char* bearer_token) {
    g_token = bearer_token ? bearer_token : "";
    if (g_token.empty()) {
        ESP_LOGW(TAG, "OTA running with NO authentication (set OTA_TOKEN to require one)");
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.ctrl_port = port + 1;
    cfg.max_uri_handlers = 4;
    cfg.recv_wait_timeout = 30;
    cfg.send_wait_timeout = 30;
    cfg.lru_purge_enable = true;

    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t ota_uri = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = handle_ota_post,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &ota_uri);

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = handle_status,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &status_uri);

    httpd_uri_t forget_uri = {
        .uri = "/forget-wifi",
        .method = HTTP_POST,
        .handler = handle_forget_wifi,
        .user_ctx = nullptr,
    };
    httpd_register_uri_handler(server, &forget_uri);

    ESP_LOGI(TAG, "OTA server listening on port %u (POST /ota, GET /status, POST /forget-wifi)", port);
    return ESP_OK;
}
