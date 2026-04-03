#include "wifi_service.h"

#include <string.h>

#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"

#include "wifi.h"
#include "uart.h"   // uart_app_write

static const char *TAG = "wifi_svc";

static QueueHandle_t s_q = NULL;
static TaskHandle_t  s_task = NULL;

// 防止同时 scan/conn 互相干扰（最简单的“忙碌锁”）
static volatile bool s_busy = false;

/* ---------------- worker: scan ---------------- */

static void scan_worker_task(void *arg)
{
    (void)arg;

    ESP_LOGI("scan", "Scan worker started...");
    wifi_scan_once_and_print_by_rssi();
    ESP_LOGI("scan", "Scan worker done.");

    s_busy = false;
    vTaskDelete(NULL);
}

/* ---------------- wifi_service task ---------------- */

static void wifi_service_task(void *arg)
{
    (void)arg;

    wifi_req_t r;

    while (1) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // 简单策略：正在执行长操作时，拒绝新请求（你也可以改成排队/覆盖）
        if (s_busy) {
            ESP_LOGI(TAG, "Busy, ignore req=%d", (int)r.type);
            continue;
        }

        switch (r.type) {
        case WIFI_REQ_SCAN: {
            s_busy = true;
            // 开一个独立线程扫描，不阻塞 cmd_task
            BaseType_t ok = xTaskCreate(scan_worker_task, "wifi_scan_w", 4096, NULL, 9, NULL);
            if (ok != pdPASS) {
                s_busy = false;
                ESP_LOGI(TAG, "Create scan worker failed");
            }
            break;
        }

        case WIFI_REQ_CONN_INDEX: {
            s_busy = true;

            ESP_LOGI("wifi", "Connect request: index=%d", r.u.conn_index.index);
            uart_app_write("\r\n", 2);
            uart_app_write("Connecting to AP #", 18);
            char idx_str[16];
            snprintf(idx_str, sizeof(idx_str), "%d", r.u.conn_index.index);
            uart_app_write(idx_str, strlen(idx_str));
            uart_app_write("...\r\n", 5);

            esp_err_t e = wifi_connect_by_index(
                r.u.conn_index.index,
                r.u.conn_index.has_psw ? r.u.conn_index.psw : NULL
            );
            ESP_LOGI("wifi", "Connect result: %s", esp_err_to_name(e));

            if (e == ESP_OK) {
                uart_app_write("Connection successful\r\n", 23);
            } else {
                int reason = wifi_get_last_disconnect_reason();
                const char *reason_str = wifi_disconnect_reason_to_str(reason);
                uart_app_write("Connection failed: ", 19);
                uart_app_write(esp_err_to_name(e), strlen(esp_err_to_name(e)));
                uart_app_write("\r\n", 2);
                if (reason != 0) {
                    uart_app_write("Reason: ", 8);
                    uart_app_write(reason_str, strlen(reason_str));
                    uart_app_write("\r\n", 2);
                }
                // 特别提示密码错误
                if (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    reason == WIFI_REASON_AUTH_FAIL ||
                    reason == WIFI_REASON_NOT_AUTHED) {
                    uart_app_write("Hint: Password may be incorrect\r\n", 34);
                }
            }

            s_busy = false;
            break;
        }

        case WIFI_REQ_CONN_SSID: {
            s_busy = true;

            ESP_LOGI("wifi", "Connect request: ssid='%s'", r.u.conn_ssid.ssid);
            uart_app_write("\r\n", 2);
            uart_app_write("Connecting to SSID: ", 20);
            uart_app_write(r.u.conn_ssid.ssid, strlen(r.u.conn_ssid.ssid));
            uart_app_write("...\r\n", 5);

            esp_err_t e = wifi_connect_by_ssid(r.u.conn_ssid.ssid, r.u.conn_ssid.psw);
            ESP_LOGI("wifi", "Connect result: %s", esp_err_to_name(e));

            if (e == ESP_OK) {
                uart_app_write("Connection successful\r\n", 23);
            } else {
                int reason = wifi_get_last_disconnect_reason();
                const char *reason_str = wifi_disconnect_reason_to_str(reason);
                uart_app_write("Connection failed: ", 19);
                uart_app_write(esp_err_to_name(e), strlen(esp_err_to_name(e)));
                uart_app_write("\r\n", 2);
                if (reason != 0) {
                    uart_app_write("Reason: ", 8);
                    uart_app_write(reason_str, strlen(reason_str));
                    uart_app_write("\r\n", 2);
                }
                // 特别提示密码错误
                if (reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    reason == WIFI_REASON_AUTH_FAIL ||
                    reason == WIFI_REASON_NOT_AUTHED) {
                    uart_app_write("Hint: Password may be incorrect\r\n", 34);
                }
            }

            s_busy = false;
            break;
        }

        case WIFI_REQ_DISCONN: {
            // 手动断开：使用wifi_disconnect_manual设置手动断开标志
            ESP_LOGI("wifi", "Manual disconnect requested");
            uart_app_write("\r\n", 2);
            uart_app_write("Manual disconnect requested\r\n", 28);
            wifi_disconnect_manual();
            uart_app_write("Disconnected. Auto-reconnect disabled.\r\n", 41);
            s_busy = false;
            break;
        }

        default:
            ESP_LOGI(TAG, "Unknown req=%d", (int)r.type);
            break;
        }
    }
}

esp_err_t wifi_service_start(void)
{
    if (s_q) return ESP_OK;

    s_q = xQueueCreate(8, sizeof(wifi_req_t));
    if (!s_q) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(wifi_service_task, "wifi_svc", 4096, NULL, 9, &s_task);
    if (ok != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
        s_task = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "wifi_service started");
    return ESP_OK;
}

esp_err_t wifi_service_post(const wifi_req_t *req)
{
    if (!s_q || !req) return ESP_ERR_INVALID_STATE;

    // 非阻塞投递：满了就失败（你也可以改成等待）
    BaseType_t ok = xQueueSend(s_q, req, 0);
    return (ok == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}
