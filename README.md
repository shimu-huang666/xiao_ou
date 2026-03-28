# ESP32 WiFi IoT 应用

一个基于 ESP32-S3 的物联网应用，集成 WiFi、MQTT、天气等功能，通过模块化设计提供灵活的功能扩展。

## 功能特性

### 🌐 WiFi 管理
- **WiFi 扫描**：扫描附近WiFi并按信号强度排序
- **连接管理**：支持多种连接方式（按索引、按SSID）
- **自动重连**：掉线自动重连上次连接的WiFi
- **NVS 持久化**：保存WiFi配置到Flash存储

### 📡 MQTT 功能
- **消息发布**：向任意主题发送消息
- **主题订阅**：支持订阅多个主题，自定义QoS
- **订阅管理**：查看、保存、恢复订阅列表
- **心跳控制**：可配置的心跳保活机制

### 🌤️ 天气服务
- **IP地址定位**：基于WiFi出口IP自动获取地理位置
- **实时天气**：集成Open-Meteo API获取天气数据
- **天气参数**：温度、湿度、风速、天气现象等

### 🛠️ 系统功能
- **NVS存储**：WiFi、MQTT订阅等配置持久化
- **异步任务**：长操作不阻塞命令行
- **Console REPL**：基于ESP-IDF Console的交互式命令行

## 快速开始

### 编译和烧录
```bash
idf.py build
idf.py flash monitor
```

### 基本命令

#### WiFi 命令
```
scan                   - 扫描附近WiFi
conn <i> [psw]         - 按索引连接WiFi（i为扫描结果的索引）
connssid <ssid> <psw>  - 按SSID和密码连接
info                   - 显示当前WiFi信息
disconn                - 手动断开连接（不自动重连）
reconn                 - 使用保存的配置重新连接
forget                 - 清空保存的WiFi配置并断开
mem                    - 显示保存的WiFi配置信息
```

#### MQTT 命令
```
mqttsend <topic> <msg> - 发送消息到主题
sub <topic> [qos]      - 订阅主题（qos: 0,1,2，默认0）
unsub <topic>          - 取消订阅
subs                   - 列出当前订阅的主题
autosub on/off         - 重连后自动恢复订阅
savesubs               - 保存订阅列表到NVS
mqtt hb on/off         - 启用/禁用心跳
hb def on/off          - 设置心跳默认状态（持久化）
```

#### 其他命令
```
weather                - 获取当前天气信息
time                   - 显示当前时间
reboot                 - 重启设备
help                   - 显示帮助信息
```

## 项目结构

```
├── main/
│   ├── main.c              - 应用入口点
│   └── CMakeLists.txt
├── components/
│   ├── user/               - 用户组件（WiFi、MQTT、天气等）
│   └── ...
├── CMakeLists.txt
├── sdkconfig              - 项目配置
└── README.md
```

## 技术栈

- **MCU**: ESP32-S3
- **开发环境**: ESP-IDF
- **网络**: WiFi、MQTT
- **存储**: NVS Flash
- **天气数据**: Open-Meteo API

## 版本历史

- **v2.2** (2026-03-10): 添加天气功能，天气任务独立运行
- **v2.1** (2026-02-08): WiFi服务层异步化，增加心跳配置
- **v2.0** (2026-02-07): MQTT订阅功能完善
- **v1.1** (2026-02-05): WiFi连接、时间显示、配置管理
- **v1.0** (2026-02-02): WiFi扫描和基础连接

## 注意事项

- 首次启动会初始化NVS存储
- WiFi配置持久化，重启后自动重连
- MQTT客户端连接到 broker.emqx.io
- 天气查询需要互联网连接
