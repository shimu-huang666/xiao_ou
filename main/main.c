/* WiFi Scan + Sort by RSSI (strong -> weak) + formatted output + UART cmd: conn <index> <psw> connect wifi by scan index
//2026/2/2 实现内容：上电初始化wifi并扫描附近的wifi，返回wifi列表。 存在问题：返回内容包含logI信息--->已解决。任务栈太小，改成8192

2/3
    实现：从station_example_main示例移植wifi连接，编译通过，已实现账密wifi连接
    跟进：从串口获取用户输入的账密

2/4 实现：  从串口获取用户输入的账密，编译通过;
            增加连接wifi后显示wifi基本参数，并可以通过info指令查询；
            增加连接wifi后可获取当前时间，并可以通过time指令；
            增加mem、forget、reconn、connssid指令

2/5 实现：  移植MQTT客户端，连接 broker.emqx.io 公共broker;
            实现 mqttsend 命令发送消息;
            增加 mqtt hb on/off 心跳控制

2/6 实现：  增加 weather 命令，基于当前WiFi出口IP获取地理位置;
            集成 Open-Meteo API 获取实时天气(温度、湿度、风速、天气现象);
            天气任务独立运行，不阻塞命令行

2/7 实现：  MQTT订阅功能升级:
            - sub <topic> [qos] 订阅主题
            - unsub <topic> 取消订阅
            - subs 列出当前订阅
            - autosub on/off 自动重连后恢复订阅
            - savesubs 保存订阅列表到NVS
            增加 reboot 命令重启设备

2/8 实现：  WiFi服务层重构，scan/conn/disconn 改为异步请求模式;
            避免长时间阻塞UART命令任务;
            增加 hb def on/off 设置心跳默认状态(NVS持久化)

2/9 优化：  代码结构优化，模块间接口清晰化;
            增加详细的命令帮助信息;
            修复若干边界情况处理

待办：
    - OTA 远程升级支持
    - LVGL 图形界面集成
    - 更多传感器驱动
*/

#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_log.h"

#include "wifi.h"
#include "uart.h"
#include "mqtt_app.h"
#include "cmd.h"
#include "weather.h"
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(uart_app_init());
    ESP_ERROR_CHECK(wifi_init_once());

    esp_err_t e = wifi_auto_connect_last();
    if (e == ESP_ERR_NOT_FOUND) {
        ESP_LOGI("main", "No last wifi record yet. Please run: scan, then conn <i> <psw> once.");
    } else if (e != ESP_OK) {
        ESP_LOGW("main", "Auto connect failed: %s", esp_err_to_name(e));
    } else {
        ESP_LOGI("main", "Auto connect started/ok.");
    }

    // 可选：启动后台自动扫一次
    ESP_ERROR_CHECK(wifi_start_bg_scan_task(NULL, 8192, 9));

    // 启动 UART 命令任务
    ESP_ERROR_CHECK(start_cmd_task("cmd", 4096, 5));

    // 启动 weather 后台任务（通过 UART 命令触发一次请求）
    ESP_ERROR_CHECK(weather_start_task(12 * 1024, 4));

    ESP_ERROR_CHECK(mqtt_app_init(
        "mqtt://broker.emqx.io",
        NULL,
        NULL,
        0,
        false
    ));
    while (1) vTaskDelay(pdMS_TO_TICKS(1000));
}
