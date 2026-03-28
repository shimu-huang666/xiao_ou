/**
 * @file console_init.c
 * @brief Console 初始化 (ESP-IDF 5.5)
 */

#include "console_init.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "wifi_service.h"

// 外部命令注册函数
extern void register_wifi_cmds(void);
extern void register_mqtt_cmds(void);
extern void register_system_cmds(void);

static const char *TAG = "console";

static void register_all_commands(void)
{
    register_wifi_cmds();
    register_mqtt_cmds();
    register_system_cmds();
}

esp_err_t console_start(void)
{
    // 启动 WiFi 服务（处理异步 scan/conn 请求）
    esp_err_t err = wifi_service_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start wifi_service: %s", esp_err_to_name(err));
        return err;
    }

    esp_console_repl_t *repl = NULL;

    // UART 配置 - 使用默认 UART0 (USB-CDC/监视器串口)
    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    // REPL 配置
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 256;
    repl_cfg.prompt = "esp32>";

    // 创建 REPL
    err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create REPL: %s", esp_err_to_name(err));
        return err;
    }

    // 注册所有命令
    register_all_commands();

    // 启动 REPL
    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start REPL: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Console REPL started on UART0");
    return ESP_OK;
}
