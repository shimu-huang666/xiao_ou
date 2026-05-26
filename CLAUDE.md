# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

基于 ESP32-S3 的 IoT 应用，使用 ESP-IDF v5.5.4 框架（C 语言），集成 WiFi 管理、MQTT 消息、天气查询和交互式命令行（Console REPL）。

## 构建与烧录

```bash
idf.py build              # 编译
idf.py flash monitor      # 烧录并打开串口监视器
idf.py menuconfig         # 打开项目配置菜单（WiFi扫描列表大小等）
idf.py clean              # 清理构建产物
```

目标芯片：ESP32-S3，使用自定义分区表（`partitions.csv`，factory 分区 1.5MB，NVS 24KB）。

## 代码架构

两层结构：`main/` 入口 + `components/user/` 所有业务逻辑。

### 入口

`main/main.c` → `app_main()` 启动顺序：NVS 初始化 → WiFi 初始化 → 自动连接上次 WiFi → 启动 Console REPL → 启动天气任务 → 初始化 MQTT。

### 核心模块（`components/user/`）

| 模块 | 文件 | 职责 |
|------|------|------|
| WiFi Core | `wifi.c/h` | WiFi STA 底层操作：扫描（RSSI 排序）、按索引/SSID 连接、自动重连、NVS 持久化、事件处理 |
| WiFi Service | `wifi_service.c/h` | 异步请求队列，将 scan/connect/disconnect 操作从命令行解耦到独立 FreeRTOS 任务 |
| MQTT App | `mqtt_app.c/h` | MQTT 客户端生命周期、订阅管理（最多 8 个主题）、心跳、NVS 持久化订阅列表、断线自动重订阅 |
| Weather | `weather.c/h` | 后台任务：ip-api.com 定位 → Open-Meteo API 获取天气，通过 FreeRTOS 队列异步执行 |
| Console | `console/console_init.c/h` | ESP-IDF Console REPL 初始化（UART0），注册所有命令 |
| Console WiFi | `console/console_wifi.c` | WiFi 命令：scan, conn, connssid, disconn, reconn, info, forget, mem |
| Console MQTT | `console/console_mqtt.c` | MQTT 命令：sub, unsub, autosub, subs, savesubs, pub, hb, mqttsend |
| Console System | `console/console_system.c` | 系统命令：time, weather, reboot, free, tasks, taskcount |
| Time Sync | `time_sync.c/h` | SNTP 时间同步（UTC+8）、时间格式化 |
| UART | `uart.c/h` | UART1 硬件驱动（GPIO 41/42, 115200），行缓冲命令解析 |
| cmd | `cmd.c/h` | **遗留代码** — 旧版 UART 命令解析器，已在 `app_main` 中被 Console REPL 替代，仍参与编译但不启动 |
| LVGL | `lvgl_api.c/h`, `user_type.h` | 占位/桩代码，为未来 LVGL 显示集成预留 |

### 关键设计模式

- **异步请求队列**：WiFi 操作通过 `wifi_req_t` 标签联合体投递到队列，由 `wifi_svc` 任务消费，命令行不阻塞。扫描操作进一步拆分为 `scan_worker_task` 子任务。
- **NVS 持久化**：WiFi 用 `"wifi_last"` 命名空间，MQTT 用 `"mqtt_app"` 命名空间。订阅列表序列化为二进制 blob（magic `0x53425553` / 'SUBS'）。
- **事件驱动 WiFi 连接**：FreeRTOS 事件组（`WIFI_CONNECTED_BIT` / `WIFI_FAIL_BIT`），15 秒超时。`manual_disconnect` 标志防止手动断开后自动重连。
- **单例初始化**：`wifi_init_once()`、`mqtt_app_start()` 用静态布尔标志确保只初始化一次。
- **后台任务 + 队列触发**：天气和 WiFi 扫描都用持久后台任务阻塞在队列上，请求到达时唤醒处理。

### 双 CLI 系统

`cmd.c` + `uart.c` 是旧版基于 UART 文本解析的命令接口（已废弃，编译但不启动）。`console/` 目录是当前使用的 ESP-IDF Console REPL，支持 argtable3 参数解析、Tab 补全和帮助文本。

## 外部服务

| 服务 | 用途 |
|------|------|
| `mqtt://broker.emqx.io` | 公共 MQTT 代理（无认证） |
| `http://ip-api.com/json/` | IP 地理定位（免费，无需 API key） |
| `https://api.open-meteo.com/v1/forecast` | 天气数据（免费，无需 API key） |
| `pool.ntp.org` | NTP 时间同步 |

## 常用工具函数

- `logi_both()`（`uart.c`）：同时写入 ESP_LOGI 和 UART 输出
- `json_get_string/double/int()`（`weather.c`）：轻量 JSON 解析（`strstr`/`sscanf`，非通用库）
- `http_get_to_buffer()`（`weather.c`）：HTTP GET 封装，响应读入缓冲区

## 已知问题

- `.gitignore` 有未解决的 Git 合并冲突标记（`<<<<<<< HEAD` / `>>>>>>> 04e2e72d`）
- `.vscode/settings.json` 中包含 DeepSeek API key，不应提交到版本控制
