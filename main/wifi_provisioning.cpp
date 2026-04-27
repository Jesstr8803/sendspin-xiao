#include "wifi_provisioning.h"

#include <cstring>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "nvs_persistence.h"

static const char* TAG = "wifi_prov";

namespace {

NvsPersistence* g_nvs = nullptr;

constexpr char HTML_FORM[] = R"HTML(
<!doctype html>
<html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>Sendspin XIAO setup</title>
<style>
body{font:16px/1.4 -apple-system,sans-serif;max-width:420px;margin:2em auto;padding:0 1em;color:#222}
h1{font-size:1.4em}
label{display:block;margin:1em 0 0.3em}
input,select{width:100%;padding:0.6em;font-size:1em;box-sizing:border-box}
button{margin-top:1.5em;padding:0.8em 1.5em;font-size:1em;background:#0a7;color:#fff;border:0;border-radius:4px}
ul{padding-left:1.2em}
.muted{color:#777;font-size:0.9em}
</style>
</head><body>
<h1>Sendspin XIAO WiFi setup</h1>
<p class=muted>Pick a network and enter the password. The device will reboot and connect.</p>
<form method=POST action=/save>
<label>Network</label>
<select name=ssid id=ssid>%SCAN%</select>
<label>Or type SSID</label>
<input name=ssid_manual placeholder="(leave blank to use the dropdown)">
<label>Password</label>
<input name=password type=password>
<button type=submit>Save and connect</button>
</form>
</body></html>
)HTML";

std::string scan_html() {
    std::string html;
    uint16_t count = 20;
    wifi_ap_record_t records[20];
    wifi_scan_config_t scan{};
    esp_wifi_scan_start(&scan, true);
    if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
        for (int i = 0; i < count; ++i) {
            std::string ssid(reinterpret_cast<char*>(records[i].ssid));
            if (ssid.empty()) continue;
            html += "<option value=\"" + ssid + "\">" + ssid + " (" +
                    std::to_string(records[i].rssi) + " dBm)</option>";
        }
    }
    if (html.empty()) html = "<option value=''>(no networks found)</option>";
    return html;
}

esp_err_t handle_root(httpd_req_t* req) {
    std::string body(HTML_FORM);
    auto pos = body.find("%SCAN%");
    if (pos != std::string::npos) body.replace(pos, 6, scan_html());
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t handle_captive(httpd_req_t* req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, nullptr, 0);
}

std::string url_decode_field(const std::string& body, const std::string& field) {
    std::string key = field + "=";
    auto p = body.find(key);
    if (p == std::string::npos) return "";
    p += key.size();
    auto end = body.find('&', p);
    std::string raw = body.substr(p, end == std::string::npos ? std::string::npos : end - p);
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '+') out += ' ';
        else if (raw[i] == '%' && i + 2 < raw.size()) {
            int hi = raw[i + 1], lo = raw[i + 2];
            auto hex = [](int c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            out += static_cast<char>((hex(hi) << 4) | hex(lo));
            i += 2;
        } else out += raw[i];
    }
    return out;
}

esp_err_t handle_save(httpd_req_t* req) {
    char buf[512];
    int total = req->content_len > (int)sizeof(buf) - 1 ? sizeof(buf) - 1 : req->content_len;
    int got = 0;
    while (got < total) {
        int n = httpd_req_recv(req, buf + got, total - got);
        if (n <= 0) break;
        got += n;
    }
    buf[got] = 0;
    std::string body(buf);

    std::string ssid = url_decode_field(body, "ssid_manual");
    if (ssid.empty()) ssid = url_decode_field(body, "ssid");
    std::string pwd = url_decode_field(body, "password");

    if (ssid.empty() || g_nvs == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    if (!g_nvs->save_wifi_credentials(ssid.c_str(), pwd.c_str())) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs write failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "saved WiFi creds for SSID '%s', rebooting", ssid.c_str());
    httpd_resp_set_type(req, "text/html");
    std::string ok_html = "<p>Saved. Device is rebooting and will join '" + ssid +
                          "'. You can close this page.</p>";
    httpd_resp_send(req, ok_html.c_str(), ok_html.size());

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
    return ESP_OK;
}

void dns_task(void*) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { vTaskDelete(nullptr); return; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        vTaskDelete(nullptr);
        return;
    }
    uint8_t buf[512];
    sockaddr_in client;
    socklen_t client_len = sizeof(client);
    while (true) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, (sockaddr*)&client, &client_len);
        if (n < 12) continue;
        // Build minimal DNS response: copy ID, set QR flag, copy question, append A 192.168.4.1.
        buf[2] = 0x81; buf[3] = 0x80;  // standard response, no error
        buf[6] = 0; buf[7] = 1;         // ANCOUNT = 1
        buf[8] = 0; buf[9] = 0;
        buf[10] = 0; buf[11] = 0;
        // Find end of question section (skip name + 4 bytes for QTYPE/QCLASS)
        int p = 12;
        while (p < n && buf[p] != 0) p += buf[p] + 1;
        p += 5;  // skip null + qtype(2) + qclass(2)
        if (p + 16 > (int)sizeof(buf)) continue;
        // Answer: pointer to name (0xc00c), TYPE A, CLASS IN, TTL 60, RDLENGTH 4, IP
        buf[p++] = 0xc0; buf[p++] = 0x0c;
        buf[p++] = 0; buf[p++] = 1;
        buf[p++] = 0; buf[p++] = 1;
        buf[p++] = 0; buf[p++] = 0; buf[p++] = 0; buf[p++] = 60;
        buf[p++] = 0; buf[p++] = 4;
        buf[p++] = 192; buf[p++] = 168; buf[p++] = 4; buf[p++] = 1;
        sendto(sock, buf, p, 0, (sockaddr*)&client, client_len);
    }
}

esp_err_t start_http_server() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.ctrl_port = 81;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    httpd_handle_t s = nullptr;
    if (httpd_start(&s, &cfg) != ESP_OK) return ESP_FAIL;

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = handle_root, .user_ctx = nullptr};
    httpd_register_uri_handler(s, &root);

    httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = handle_save, .user_ctx = nullptr};
    httpd_register_uri_handler(s, &save);

    // Captive-portal triggers used by Apple, Google, Microsoft.
    for (const char* path : {"/generate_204", "/hotspot-detect.html", "/connecttest.txt",
                              "/redirect", "/ncsi.txt"}) {
        httpd_uri_t u = {.uri = path, .method = HTTP_GET, .handler = handle_captive, .user_ctx = nullptr};
        httpd_register_uri_handler(s, &u);
    }
    return ESP_OK;
}

}  // namespace

esp_err_t wifi_provisioning_start(NvsPersistence& nvs, const char* ap_ssid_prefix) {
    g_nvs = &nvs;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ssid[33];
    std::snprintf(ssid, sizeof(ssid), "%s-%02X%02X%02X",
                  ap_ssid_prefix, mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "starting SoftAP '%s' (open) at 192.168.4.1", ssid);

    // Re-init netif so we can switch to AP+STA mode (STA needed for scanning).
    esp_netif_create_default_wifi_ap();
    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == nullptr) {
        esp_netif_create_default_wifi_sta();
    }

    wifi_config_t ap{};
    std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = std::strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (start_http_server() != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed");
        return ESP_FAIL;
    }
    xTaskCreate(dns_task, "dns_hijack", 4096, nullptr, 4, nullptr);

    ESP_LOGI(TAG, "Provisioning ready: connect to '%s', browse to anything.", ssid);
    return ESP_OK;
}
