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

---

# ESP-SR 语音控制集成方案

## Context

项目已有 ESP-SR v2.0.0 源码（`esp-sr-2.0.0/esp-sr-2.0.0/`），但未接入构建系统。需要实现：唤醒词检测（WakeNet）+ 语音命令识别（MultiNet），并通过 AFE（音频前端）处理麦克风输入。

## 前置条件

- ESP32-S3（16MB Flash，有 PSRAM）
- 麦克风硬件（I2S PDM 或 I2S 标准模式，具体型号待定）
- ESP-SR v2.0.0 源码已在 `esp-sr-2.0.0/esp-sr-2.0.0/`

## 完整调用流程

```
I2S 麦克风 → AFE(feed) → AFE(fetch) → wakeup_state?
                                          ├─ WAKENET_DETECTED → MultiNet(detect) → command_id → 执行动作
                                          └─ WAKENET_NO_DETECT → 继续 feed/fetch
```

---

## Step 1: 接入 ESP-SR 到构建系统

### 1.1 移动组件

将 `esp-sr-2.0.0/esp-sr-2.0.0/` 移动（或软链接）到 `components/esp-sr/`

### 1.2 修改 `partitions.csv`

添加 model 分区（存放唤醒词和 MultiNet 模型）：

```
# Name,    Type, SubType, Offset,    Size
nvs,       data, nvs,     0x9000,    0x6000,
phy_init,  data, phy,     0xf000,    0x1000,
otadata,   data, ota,     0x10000,   0x2000,
ota_0,     app,  ota_0,   0x12000,   0x180000,
ota_1,     app,  ota_1,   0x192000,  0x180000,
model,     data, spiffs,  0x312000,  0x100000,
```

model 分区 1MB，存放 srmodels.bin。

### 1.3 修改 `components/user/CMakeLists.txt`

```cmake
REQUIRES 添加 esp-sr
```

### 1.4 修改 `sdkconfig.defaults`

```
CONFIG_ESP32S3_SPIRAM_SUPPORT=y
```

ESP-SR 需要 PSRAM 加载模型。

---

## Step 2: I2S 麦克风驱动

新建 `components/user/mic.c` + `mic.h`

```c
// 初始化 I2S 麦克风（PDM 模式，16kHz，16bit）
esp_err_t mic_init(void);

// 读取一帧音频数据到 buffer，返回实际读取的样本数
int mic_read(int16_t *buffer, int samples_needed);

// 获取每帧样本数（与 AFE feed_chunksize 对齐）
int mic_get_chunksize(void);
```

使用 `driver/i2s_std.h` 或 `driver/i2s_pdm.h`（ESP-IDF v5.x 新驱动），配置：
- 采样率：16000 Hz
- 位宽：16 bit
- 通道：1（单声道）或 2（双声道，取决于硬件）

---

## Step 3: 语音控制核心模块

新建 `components/user/speech.c` + `speech.h`

### 3.1 完整初始化流程

```c
#include "model_path.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"

static srmodel_list_t *s_models = NULL;
static esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static esp_mn_iface_t *s_multinet = NULL;
static model_iface_data_t *s_mn_model = NULL;

esp_err_t speech_init(void)
{
    // 1. 从 flash model 分区加载所有模型
    s_models = esp_srmodel_init("model");
    if (!s_models) return ESP_FAIL;

    // 2. 查找唤醒词模型（如 "wn9_hilexin"）
    char *wn_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (!wn_name) return ESP_FAIL;

    // 3. 查找中文 MultiNet 模型（如 "mn6_cn"）
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_CHINESE);
    if (!mn_name) return ESP_FAIL;

    // 4. 创建 AFE 配置（"MR" = 1麦克风 + 1参考信号，用于 AEC）
    afe_config_t *afe_config = afe_config_init("MR", s_models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);

    // 5. 可选：微调 AFE 参数
    afe_config->vad_init = true;
    afe_config->vad_mode = VAD_MODE_3;
    afe_config->wakenet_init = true;
    afe_config->wakenet_model_name = wn_name;

    // 6. 获取 AFE handle 并创建实例
    s_afe_handle = esp_afe_handle_from_config(afe_config);
    s_afe_data = s_afe_handle->create_from_config(afe_config);

    // 7. 获取帧大小，与 I2S 对齐
    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int feed_nch = s_afe_handle->get_feed_channel_num(s_afe_data);

    // 8. 创建 MultiNet 实例（超时 6000ms）
    s_multinet = esp_mn_handle_from_name(mn_name);
    s_mn_model = s_multinet->create(mn_name, 6000);

    // 9. 注册语音命令（拼音形式）
    esp_mn_commands_alloc(s_multinet, s_mn_model);
    esp_mn_commands_add(1, "da kai kong tiao");     // 打开空调
    esp_mn_commands_add(2, "guan bi kong tiao");     // 关闭空调
    esp_mn_commands_add(3, "da kai deng guang");     // 打开灯光
    esp_mn_commands_add(4, "guan bi deng guang");     // 关闭灯光
    esp_mn_error_t *err = esp_mn_commands_update();
    if (err) {
        ESP_LOGE("speech", "Command update failed: %s", err->phrase_str);
        return ESP_FAIL;
    }

    afe_config_free(afe_config);
    return ESP_OK;
}
```

### 3.2 语音处理主循环

```c
void speech_task(void *arg)
{
    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    int feed_nch = s_afe_handle->get_feed_channel_num(s_afe_data);
    int16_t *feed_buff = heap_caps_malloc(feed_chunksize * feed_nch * sizeof(int16_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    while (1) {
        // A. 从麦克风读取音频，送入 AFE
        mic_read(feed_buff, feed_chunksize * feed_nch);
        s_afe_handle->feed(s_afe_data, feed_buff);

        // B. 获取 AFE 处理结果
        afe_fetch_result_t *result = s_afe_handle->fetch(s_afe_data);
        if (result->ret_value == ESP_FAIL) break;

        // C. 检查唤醒词
        if (result->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI("speech", "Wakeup detected!");
            s_afe_handle->disable_wakenet(s_afe_data);

            // D. 唤醒后进入命令识别阶段（~6秒超时）
            int timeout_counter = 0;
            while (timeout_counter < 300) {
                mic_read(feed_buff, feed_chunksize * feed_nch);
                s_afe_handle->feed(s_afe_data, feed_buff);
                result = s_afe_handle->fetch(s_afe_data);

                esp_mn_state_t mn_state = s_multinet->detect(s_mn_model, result->data);

                if (mn_state == ESP_MN_STATE_DETECTED) {
                    esp_mn_results_t *mn_result = s_multinet->get_results(s_mn_model);
                    if (mn_result->num > 0) {
                        ESP_LOGI("speech", "Command: id=%d, str=%s",
                                 mn_result->command_id[0], mn_result->string);
                        speech_handle_command(mn_result->command_id[0]);
                    }
                    break;
                }
                if (mn_state == ESP_MN_STATE_TIMEOUT) break;
                timeout_counter++;
            }

            s_afe_handle->enable_wakenet(s_afe_data);
        }
    }
    free(feed_buff);
    vTaskDelete(NULL);
}
```

### 3.3 命令执行回调

```c
void speech_handle_command(int command_id)
{
    switch (command_id) {
    case 1:  mqtt_app_publish("/home/ac", "on", 0);    break;
    case 2:  mqtt_app_publish("/home/ac", "off", 0);   break;
    case 3:  mqtt_app_publish("/home/light", "on", 0);  break;
    case 4:  mqtt_app_publish("/home/light", "off", 0); break;
    default: ESP_LOGW("speech", "Unknown command: %d", command_id);
    }
}
```

---

## Step 4: 集成到 main.c

```c
// app_main() 中，在 mqtt_app_init() 之后：
ESP_ERROR_CHECK(mic_init());
ESP_ERROR_CHECK(speech_init());
xTaskCreatePinnedToCore(speech_task, "speech", 8 * 1024, NULL, 5, NULL, 1);
```

---

## 关键 API 速查表

| 功能 | API | 头文件 |
|------|-----|--------|
| 加载模型 | `esp_srmodel_init("model")` | `model_path.h` |
| 按前缀查找模型 | `esp_srmodel_filter(models, "wn", NULL)` | `model_path.h` |
| 创建 AFE 配置 | `afe_config_init("MR", models, AFE_TYPE_SR, AFE_MODE_HIGH_PERF)` | `esp_afe_config.h` |
| 获取 AFE handle | `esp_afe_handle_from_config(config)` | `esp_afe_sr_models.h` |
| 创建 AFE 实例 | `afe_handle->create_from_config(config)` | `esp_afe_sr_iface.h` |
| 送入音频 | `afe_handle->feed(afe_data, buffer)` | `esp_afe_sr_iface.h` |
| 获取结果 | `afe_handle->fetch(afe_data)` | `esp_afe_sr_iface.h` |
| 唤醒状态 | `result->wakeup_state == WAKENET_DETECTED` | `esp_afe_sr_iface.h` |
| 暂停/恢复唤醒 | `afe_handle->disable_wakenet()` / `enable_wakenet()` | `esp_afe_sr_iface.h` |
| 创建 MultiNet | `multinet->create(model_name, 6000)` | `esp_mn_iface.h` |
| 添加命令 | `esp_mn_commands_add(id, "pinyin")` | `esp_mn_speech_commands.h` |
| 应用命令 | `esp_mn_commands_update()` | `esp_mn_speech_commands.h` |
| 识别命令 | `multinet->detect(model, audio_data)` | `esp_mn_iface.h` |
| 获取结果 | `multinet->get_results(model)` | `esp_mn_iface.h` |

## 可用唤醒词（ESP-SR v2.0.0 内置）

| 模型名 | 唤醒词 |
|--------|--------|
| `wn9_hilexin` | Hi, ESP |
| `wn9_hiesp` | Hi, ESP |
| `wn9_alexa` | Alexa |
| `wn9_xiaoaitongxue` | 小爱同学 |
| `wn9_nihaoxiaozhi_tts` | 你好小智 |
| `wn9_computer_tts` | Computer |
| `wn9_jarvis_tts` | Jarvis |

## 涉及文件

| 操作 | 文件 |
|------|------|
| 移动 | `esp-sr-2.0.0/esp-sr-2.0.0/` → `components/esp-sr/` |
| 修改 | `partitions.csv`（添加 model 分区） |
| 修改 | `sdkconfig.defaults`（PSRAM 支持） |
| 修改 | `components/user/CMakeLists.txt`（添加 esp-sr 依赖） |
| 新建 | `components/user/mic.c` + `mic.h`（I2S 麦克风驱动） |
| 新建 | `components/user/speech.c` + `speech.h`（语音控制核心） |
| 新建 | `console/console_speech.c`（可选） |
| 修改 | `main/main.c`（初始化 + 启动任务） |

## 验证方式

1. `idf.py build` 编译通过
2. 烧录后串口输入 `speech_status` 查看模型加载状态
3. 对麦克风说唤醒词，串口日志显示 "Wakeup: xxx"
4. 说语音命令，串口日志显示 "Command: id=x, str=xxx"
5. MQTT 消息发送到对应主题
