# OTA 远程固件更新 + LVGL 显示框架 实施计划

## Part 1: OTA 远程固件更新

### 1.1 修改分区表 `partitions.csv`

当前是单分区布局（factory 1.5MB），改为双 OTA 分区：

```
# Name,    Type, SubType, Offset,    Size
nvs,       data, nvs,     0x9000,    0x6000,
phy_init,  data, phy,     0xf000,    0x1000,
otadata,   data, ota,     0x10000,   0x2000,
ota_0,     app,  ota_0,   0x12000,   0x180000,
ota_1,     app,  ota_1,   0x192000,  0x180000,
```

需要 4MB+ flash（本ESP32-S3 16MB Flash，无问题）。

### 1.2 修改 `sdkconfig.defaults`

添加：

```
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
```

### 1.3 新建 `components/user/ota.c` + `ota.h`

核心函数：

- `esp_err_t ota_start(const char *url)` — 使用 `esp_https_ota()` 从 URL 下载并写入固件
- 内部使用 `esp_crt_bundle_attach` 做 TLS（复用 weather.c 的模式）
- 下载完成后设置标记，等待 reboot 生效

安全机制：

- 下载前检查目标分区大小
- 使用 `esp_https_ota_config_t` 配置 HTTP 客户端超时
- `esp_ota_mark_app_valid_cancel_rollback()` 在新固件首次成功启动后调用

### 1.4 新建 `console/console_ota.c`

注册命令：

- `ota <url>` — 从指定 URL 下载并应用固件，完成后提示 reboot
- `ota_status` — 显示当前运行的 OTA 分区和固件版本

### 1.5 修改 `main/main.c`

在 `app_main()` 中添加首次启动标记：

```c
const esp_partition_t *running = esp_ota_get_running_partition();
esp_ota_img_states_t state;
if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
}
```

### 1.6 修改 `components/user/CMakeLists.txt`

SRCS 添加 `"ota.c"` `"console/console_ota.c"`

### 涉及文件

| 操作 | 文件                                                        |
| ---- | ----------------------------------------------------------- |
| 修改 | `partitions.csv`                                          |
| 修改 | `sdkconfig.defaults`                                      |
| 新建 | `components/user/ota.c`                                   |
| 新建 | `components/user/ota.h`                                   |
| 新建 | `console/console_ota.c`                                   |
| 修改 | `components/user/console/console_init.c`（注册 ota 命令） |
| 修改 | `components/user/CMakeLists.txt`                          |
| 修改 | `main/main.c`                                             |

---

## Part 2: LVGL 显示框架

### 2.1 安装 LVGL 组件

创建 `components/user/idf_component.yml`：

```yaml
dependencies:
  lvgl/lvgl: "~9.1"
  esp_lvgl_port: "~2.4"
```

`esp_lvgl_port` 是乐鑫官方的 LVGL 移植层，封装了 tick/flush/task。

### 2.2 修改 `components/user/CMakeLists.txt`

REQUIRES 添加 `lvgl`、`esp_lcd`、`esp_lvgl_port`

### 2.3 重构 `components/user/lvgl_api.h`

定义抽象显示驱动接口：

```c
typedef struct {
    uint16_t width;
    uint16_t height;
    // 后续补充：SPI 引脚、I2C 地址等
} display_config_t;

esp_err_t display_init(const display_config_t *config);
lv_display_t *display_get(void);
```

以及 UI 层函数：

```c
void ui_init(void);
void ui_update_time(const lv_time_info *time);
void ui_update_weather(const char *desc, float temp, float humidity);
void ui_update_wifi_status(bool connected, const char *ssid);
void ui_update_mqtt_status(bool connected, int sub_count);
```

### 2.4 实现 `components/user/lvgl_api.c`

- `display_init()` — 暂为空实现（桩），等硬件确定后填充 SPI/I2C + esp_lcd 面板初始化
- `ui_init()` — 创建基本 UI 布局（时间标签、天气标签、WiFi/MQTT 状态图标），使用 LVGL v9 API
- `ui_update_*()` — 更新各标签文本

### 2.5 修改 `main/main.c`

在 console_start() 之后、while(1) 之前：

```c
display_init(&default_config);
ui_init();
```

### 2.6 修复 `user_type.h` 的 bug

`lv_time_info.year` 从 `int8_t` 改为 `int16_t`（当前会溢出，2026 超出 int8_t 范围）。

### 涉及文件

| 操作 | 文件                                              |
| ---- | ------------------------------------------------- |
| 新建 | `components/user/idf_component.yml`             |
| 修改 | `components/user/CMakeLists.txt`                |
| 修改 | `components/user/lvgl_api.h`                    |
| 修改 | `components/user/lvgl_api.c`                    |
| 修改 | `components/user/user_type.h`（修复 year 溢出） |
| 修改 | `main/main.c`                                   |

---

## 执行顺序

1. **OTA 分区表 + sdkconfig** → 验证：`idf.py build` 编译通过
2. **OTA 模块 + 控制台命令** → 验证：编译通过，串口输入 `ota --help` 显示帮助
3. **LVGL 组件安装 + 框架代码** → 验证：`idf.py build` 编译通过（显示驱动为桩）
4. **user_type.h bug 修复** → 验证：编译通过

## 验证方式

- **OTA**：`idf.py build` 成功；烧录后串口输入 `ota_status` 显示当前分区信息；如有 HTTP 服务器可测试 `ota http://<ip>/firmware.bin`
- **LVGL**：`idf.py build` 成功；`ui_init()` 创建的 UI 在连接屏幕后可见（需确定硬件后补充驱动）
