#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi_types.h"

#include "time_sync.h"
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "uart.h"   // uart_app_write / uart_app_get_cmd_queue / uart_cmd_msg_t
#include "mqtt_app.h"

/**
 * @brief 初始化 Wi-Fi（只会执行一次）
 */
esp_err_t wifi_init_once(void);

/**
 * @brief 扫描一次 + 按 RSSI 从强到弱排序 + 打印结果，并更新内部缓存（索引顺序=打印顺序）
 */
void wifi_scan_once_and_print_sorted(void);

/**
 * @brief 自动连接上一次保存的 AP（NVS 记录）
 */
esp_err_t wifi_auto_connect_last(void);

/**
 * @brief 根据 scan 的 1-based 索引连接（conn <index> <psw>）
 * @param idx_1based  1..N
 * @param psw_opt     NULL 或 "" 表示沿用已保存密码（或空密码）
 */
esp_err_t wifi_connect_by_index(int idx_1based, const char *psw_opt);

/**
 * @brief 返回当前扫描缓存条目数
 */
uint16_t wifi_get_scan_cache_count(void);

/**
 * @brief 启动后台扫描任务：启动后扫描一次，然后尝试自动连接上次 WiFi
 */
esp_err_t wifi_start_bg_scan_task(const char *task_name, uint32_t stack_words, UBaseType_t prio);


/**
 * @brief 清除上次保存的 AP（NVS: ssid/bssid/auth），并可选清除 flash 里保存的 STA 配置
 * @param clear_wifi_flash_cfg  true: 同时清空 esp_wifi 保存的 STA ssid/psw
 */
esp_err_t wifi_forget_last(bool clear_wifi_flash_cfg);

/**
 * @brief 直接用 SSID+密码连接（不依赖 scan 缓存）
 */
esp_err_t wifi_connect_by_ssid(const char *ssid, const char *psw);

/**
 * @brief 打印当前记忆（NVS last AP）和当前 STA 配置（esp_wifi 保存的）
 */
void wifi_print_memory(void);
void wifi_print_info(void);
esp_err_t wifi_reconnect_saved(void);

/**
 * @brief 获取最后一次WiFi断开原因代码
 * @return 断开原因代码，0表示无断开记录
 */
int wifi_get_last_disconnect_reason(void);

/**
 * @brief 将WiFi断开原因代码转换为可读字符串
 * @param reason 断开原因代码
 * @return 描述字符串
 */
const char* wifi_disconnect_reason_to_str(int reason);

/**
 * @brief 是否已连接 WiFi 并获取到 IP
 */
bool wifi_is_connected(void);

/**
 * @brief 手动断开WiFi连接（设置手动断开标志，防止自动重连）
 */
void wifi_disconnect_manual(void);

#ifdef __cplusplus
}
#endif
