#pragma once

#include "esp_err.h"

/**
 * @brief 启动 ESP-IDF Console REPL
 *
 * 初始化 UART console，注册所有命令，启动 REPL 任务
 *
 * @return esp_err_t
 *   - ESP_OK: 成功
 *   - 其他: 失败
 */
esp_err_t console_start(void);
