/*
 * WiFi Scan + Sort by RSSI + UART CLI
 * Commands:
 *   scan
 *   conn <index> [psw]
 *   info
 *   time
 *   disconn
 *   help
 */

#include "wifi.h"


/* -------------------------- Config -------------------------- */

#ifndef CONFIG_EXAMPLE_SCAN_LIST_SIZE
#define CONFIG_EXAMPLE_SCAN_LIST_SIZE 20
#endif

#ifndef WIFI_MAXIMUM_RETRY
#define WIFI_MAXIMUM_RETRY  3
#endif

#define WIFI_CONNECT_TIMEOUT_MS 15000

#define NVS_NS_WIFI   "wifi_last"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_BSSID "bssid"
#define NVS_KEY_AUTH  "auth"

/* Event bits */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG_WIFI = "wifi";
static const char *TAG_SCAN = "scan";


/* -------------------------- Context -------------------------- */

typedef struct {
    EventGroupHandle_t wifi_event_group;
    bool inited;
    bool connected;          // got IP
    bool manual_disconnect;
    int  retry_num;
    int  last_disconnect_reason;  // 存储最近一次WiFi断开原因

    esp_netif_t *sta_netif;

    wifi_ap_record_t ap_cache[CONFIG_EXAMPLE_SCAN_LIST_SIZE];
    uint16_t ap_cache_num;

    esp_event_handler_instance_t h_wifi_any;
    esp_event_handler_instance_t h_got_ip;
} wifi_ctx_t;

static wifi_ctx_t s = {0};


/* -------------------------- Helpers -------------------------- */

static const char *authmode_str(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:                 return "OPEN";
    case WIFI_AUTH_OWE:                  return "OWE";
    case WIFI_AUTH_WEP:                  return "WEP";
    case WIFI_AUTH_WPA_PSK:              return "WPA";
    case WIFI_AUTH_WPA2_PSK:             return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:         return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:             return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:        return "WPA2/WPA3";
    case WIFI_AUTH_ENTERPRISE:           return "ENT";
    case WIFI_AUTH_WPA3_ENTERPRISE:      return "WPA3-ENT";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/WPA3-ENT";
    case WIFI_AUTH_WPA3_ENT_192:         return "WPA3-192";
    default:                             return "UNK";
    }
}

static const char *cipher_str(wifi_cipher_type_t c)
{
    switch (c) {
    case WIFI_CIPHER_TYPE_NONE:        return "NONE";
    case WIFI_CIPHER_TYPE_WEP40:       return "WEP40";
    case WIFI_CIPHER_TYPE_WEP104:      return "WEP104";
    case WIFI_CIPHER_TYPE_TKIP:        return "TKIP";
    case WIFI_CIPHER_TYPE_CCMP:        return "CCMP";
    case WIFI_CIPHER_TYPE_TKIP_CCMP:   return "TKIP/CCMP";
    case WIFI_CIPHER_TYPE_AES_CMAC128: return "AES-CMAC";
    case WIFI_CIPHER_TYPE_SMS4:        return "SMS4";
    case WIFI_CIPHER_TYPE_GCMP:        return "GCMP";
    case WIFI_CIPHER_TYPE_GCMP256:     return "GCMP256";
    default:                           return "UNK";
    }
}

static void bssid_to_str(const uint8_t bssid[6], char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static int cmp_ap_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *ra = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *rb = (const wifi_ap_record_t *)b;

    if (ra->rssi > rb->rssi) return -1;
    if (ra->rssi < rb->rssi) return  1;

    if (ra->primary < rb->primary) return -1;
    if (ra->primary > rb->primary) return  1;

    return strncmp((const char *)ra->ssid, (const char *)rb->ssid, sizeof(ra->ssid));
}

/* -------------------------- NVS last AP -------------------------- */

typedef struct {
    char ssid[33];
    uint8_t bssid[6];
    wifi_auth_mode_t authmode;
    bool has_bssid;
    bool valid;
} wifi_last_ap_t;

static esp_err_t wifi_save_last_ap(const wifi_ap_record_t *ap)
{
    //打开命名空间
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    char ssid[33] = {0};
    memcpy(ssid, ap->ssid, sizeof(ap->ssid));
    ssid[32] = 0;
    //写入Wifi信息
    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_blob(h, NVS_KEY_BSSID, ap->bssid, 6);
    if (err == ESP_OK) err = nvs_set_u8(h, NVS_KEY_AUTH, (uint8_t)ap->authmode);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    return err;
}

static wifi_last_ap_t wifi_load_last_ap(void)
{
    wifi_last_ap_t out = {0};

    nvs_handle_t h;
    if (nvs_open(NVS_NS_WIFI, NVS_READONLY, &h) != ESP_OK) return out;

    size_t len = sizeof(out.ssid);
    //读取
    if (nvs_get_str(h, NVS_KEY_SSID, out.ssid, &len) == ESP_OK) {
        size_t blen = 6;
        if (nvs_get_blob(h, NVS_KEY_BSSID, out.bssid, &blen) == ESP_OK && blen == 6) {
            out.has_bssid = true;
        }
        uint8_t a = 0;
        if (nvs_get_u8(h, NVS_KEY_AUTH, &a) == ESP_OK) {
            out.authmode = (wifi_auth_mode_t)a;
        } else {
            out.authmode = WIFI_AUTH_WPA2_PSK;
        }
        out.valid = true;
    }
    nvs_close(h);
    return out;
}

static int cache_find_by_bssid(const uint8_t bssid[6])
{
    for (int i = 0; i < (int)s.ap_cache_num; i++) {
        if (memcmp(s.ap_cache[i].bssid, bssid, 6) == 0) return i;
    }
    return -1;
}

/* -------------------------- WiFi info -------------------------- */

void wifi_print_info(void)
{
    wifi_ap_record_t ap;
    esp_err_t err_ap = esp_wifi_sta_get_ap_info(&ap);

    if (!s.connected || err_ap != ESP_OK) {
        uint8_t mac[6] = {0};
        esp_wifi_get_mac(WIFI_IF_STA, mac);

        ESP_LOGI(TAG_WIFI, "WiFi status: NOT CONNECTED");
        ESP_LOGI(TAG_WIFI, "STA MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        if (err_ap != ESP_OK) {
            ESP_LOGI(TAG_WIFI, "sta_get_ap_info failed: %s", esp_err_to_name(err_ap));
        }
        return;
    }

    char bssid[18];
    bssid_to_str(ap.bssid, bssid);

    ESP_LOGI(TAG_WIFI, "WiFi status: CONNECTED");
    ESP_LOGI(TAG_WIFI, "SSID: %s", (char *)ap.ssid);
    ESP_LOGI(TAG_WIFI, "BSSID: %s", bssid);
    ESP_LOGI(TAG_WIFI, "Channel: %d", ap.primary);
    ESP_LOGI(TAG_WIFI, "RSSI: %d dBm", ap.rssi);
    ESP_LOGI(TAG_WIFI, "Auth: %s", authmode_str(ap.authmode));
    ESP_LOGI(TAG_WIFI, "Pairwise: %s", cipher_str(ap.pairwise_cipher));
    ESP_LOGI(TAG_WIFI, "Group: %s", cipher_str(ap.group_cipher));

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_LOGI(TAG_WIFI, "STA MAC: %02x:%02x:%02x:%02x:%02x:%02x",
              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (s.sta_netif) {
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s.sta_netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG_WIFI, "GW: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG_WIFI, "MASK: " IPSTR, IP2STR(&ip_info.netmask));
        }

        esp_netif_dns_info_t dns;
        if (esp_netif_get_dns_info(s.sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "DNS1: " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        }
        if (esp_netif_get_dns_info(s.sta_netif, ESP_NETIF_DNS_BACKUP, &dns) == ESP_OK) {
            ESP_LOGI(TAG_WIFI, "DNS2: " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        }
    }
}
void wifi_print_memory(void)
{
    // 1) NVS: last AP record
    wifi_last_ap_t last = wifi_load_last_ap();

    if (!last.valid) {
        ESP_LOGI(TAG_WIFI, "[MEM] NVS last AP: <empty>");
    } else {
        if (last.has_bssid) {
            char bssid[18];
            bssid_to_str(last.bssid, bssid);
            ESP_LOGI(TAG_WIFI, "[MEM] NVS last AP: ssid='%s' bssid=%s auth=%s",
                      last.ssid, bssid, authmode_str(last.authmode));
        } else {
            ESP_LOGI(TAG_WIFI, "[MEM] NVS last AP: ssid='%s' bssid=<none> auth=%s",
                      last.ssid, authmode_str(last.authmode));
        }
    }

    // 2) esp_wifi flash config: STA saved config
    wifi_config_t cfg = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGI(TAG_WIFI, "[MEM] STA cfg (flash): read failed: %s", esp_err_to_name(err));
        return;
    }

    const char *ssid = (const char *)cfg.sta.ssid;
    bool has_ssid = ssid[0] != '\0';
    bool has_psw  = cfg.sta.password[0] != '\0';

    ESP_LOGI(TAG_WIFI, "[MEM] STA cfg (flash): ssid=%s, password=%s, bssid_set=%d",
              has_ssid ? ssid : "<empty>",
              has_psw ? "<set>" : "<empty>",
              (int)cfg.sta.bssid_set);

    if (cfg.sta.bssid_set) {
        char bssid[18];
        bssid_to_str(cfg.sta.bssid, bssid);
        ESP_LOGI(TAG_WIFI, "[MEM] STA cfg (flash): bssid=%s", bssid);
    }
}


/* -------------------------- Event handler -------------------------- */

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    (void)arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_WIFI, "STA_START");
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s.connected = false;

        wifi_event_sta_disconnected_t *dis = (wifi_event_sta_disconnected_t *)data;
        s.last_disconnect_reason = dis ? dis->reason : -1;
        ESP_LOGI(TAG_WIFI, "DISCONNECTED, reason=%d", s.last_disconnect_reason);

        if (s.manual_disconnect) {
            ESP_LOGI(TAG_WIFI, "Manual disconnect -> skip auto reconnect");
            s.retry_num = 0;
            xEventGroupClearBits(s.wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(s.wifi_event_group, WIFI_FAIL_BIT);
            return;
        }

        if (s.retry_num < WIFI_MAXIMUM_RETRY) {
            s.retry_num++;
            ESP_LOGI(TAG_WIFI, "retry %d/%d", s.retry_num, WIFI_MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s.wifi_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s.connected = true;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        ESP_LOGI(TAG_WIFI, "GOT_IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s.retry_num = 0;
        s.last_disconnect_reason = 0;  // 清除上次断开原因
        xEventGroupSetBits(s.wifi_event_group, WIFI_CONNECTED_BIT);
        return;
    }
}

/* -------------------------- Public API -------------------------- */

esp_err_t wifi_init_once(void)
{
    if (s.inited) return ESP_OK;

    s.wifi_event_group = xEventGroupCreate();
    if (!s.wifi_event_group) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    if (!s.sta_netif) {
        s.sta_netif = esp_netif_create_default_wifi_sta();
        if (!s.sta_netif) return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s.h_wifi_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s.h_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s.inited = true;
    ESP_LOGI(TAG_WIFI, "wifi_init_once done");
    return ESP_OK;
}
void wifi_scan_once_and_print_sorted(void)
{
    if (wifi_init_once() != ESP_OK) {
        ESP_LOGI(TAG_SCAN, "wifi init failed");
        return;
    }

    memset(s.ap_cache, 0, sizeof(s.ap_cache));
    s.ap_cache_num = 0;

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true
    };

    ESP_LOGI(TAG_SCAN, "Start scan...");
    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true); // block=true
    if (err != ESP_OK) {
        ESP_LOGI(TAG_SCAN, "scan start failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG_SCAN, "Total APs scanned = %u", ap_count);

    uint16_t number = CONFIG_EXAMPLE_SCAN_LIST_SIZE;
    if (number > ap_count) number = ap_count;

    wifi_ap_record_t ap_info[CONFIG_EXAMPLE_SCAN_LIST_SIZE];
    memset(ap_info, 0, sizeof(ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));

    qsort(ap_info, number, sizeof(wifi_ap_record_t), cmp_ap_rssi_desc);

    s.ap_cache_num = number;
    if (number > 0) {
        memcpy(s.ap_cache, ap_info, number * sizeof(wifi_ap_record_t));
    }

    ESP_LOGI(TAG_SCAN, "-------- WIFI SCAN RESULT (sorted by RSSI) --------------");
    ESP_LOGI(TAG_SCAN, "%-3s %-32s %-3s %-10s %-9s %-9s %-17s %-5s",
              "No", "SSID", "CH", "AUTH", "PAIR", "GROUP", "BSSID", "RSSI");
    ESP_LOGI(TAG_SCAN, "------------------------------------------------------------------------------------------------------");

    for (int i = 0; i < number; i++) {
        char bssid[18];
        bssid_to_str(ap_info[i].bssid, bssid);

        // SSID 不是保证 '\0' 结尾的：做一个安全拷贝
        char ssid[33];
        memcpy(ssid, ap_info[i].ssid, 32);
        ssid[32] = '\0';

        ESP_LOGI(TAG_SCAN, "%-3d %-32.32s %-3d %-10s %-9s %-9s %-17s %-5d",
                  i + 1,
                  ssid,
                  ap_info[i].primary,
                  authmode_str(ap_info[i].authmode),
                  cipher_str(ap_info[i].pairwise_cipher),
                  cipher_str(ap_info[i].group_cipher),
                  bssid,
                  ap_info[i].rssi);
    }

    ESP_LOGI(TAG_SCAN, "------------------------------------------------------------------------------------------------------");
}

esp_err_t wifi_connect_by_index(int idx_1based, const char *psw_opt)
{
    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;

    if (s.ap_cache_num == 0) {
        ESP_LOGI(TAG_SCAN, "No scan cache. Please run: scan");
        return ESP_FAIL;
    }
    if (idx_1based < 1 || idx_1based > (int)s.ap_cache_num) {
        ESP_LOGI(TAG_SCAN, "Index out of range. Valid: 1..%u", s.ap_cache_num);
        return ESP_ERR_INVALID_ARG;
    }

    const wifi_ap_record_t *ap = &s.ap_cache[idx_1based - 1];

    wifi_config_t wifi_config = {0};

    memcpy(wifi_config.sta.ssid, ap->ssid, sizeof(wifi_config.sta.ssid));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    if (psw_opt && psw_opt[0]) {
        strncpy((char *)wifi_config.sta.password, psw_opt, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    } else {
        wifi_config_t saved = {0};
        if (esp_wifi_get_config(WIFI_IF_STA, &saved) == ESP_OK) {
            strncpy((char *)wifi_config.sta.password,
                    (const char *)saved.sta.password,
                    sizeof(wifi_config.sta.password) - 1);
            wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
        } else {
            wifi_config.sta.password[0] = '\0';
        }
    }

    wifi_config.sta.threshold.authmode = ap->authmode;
    wifi_config.sta.bssid_set = 1;
    memcpy(wifi_config.sta.bssid, ap->bssid, 6);

    xEventGroupClearBits(s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s.retry_num = 0;
    s.last_disconnect_reason = 0;  // 清除上次断开原因

    ESP_LOGI(TAG_WIFI, "Target AP: ssid='%s' bssid=%02x:%02x:%02x:%02x:%02x:%02x ch=%d rssi=%d auth=%s",
              (char *)ap->ssid,
              ap->bssid[0], ap->bssid[1], ap->bssid[2], ap->bssid[3], ap->bssid[4], ap->bssid[5],
              ap->primary, ap->rssi, authmode_str(ap->authmode));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_disconnect();
    s.manual_disconnect = false;
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(
        s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Connected OK.");
        wifi_print_info();
        time_sync_init();
        wifi_save_last_ap(ap);
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Connect failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_WIFI, "Connecting... (timeout, keep retry in background)");
    return ESP_OK;
}

/**
 * @brief 自动连接上次保存的WiFi热点
 *
 * 功能：从NVS读取上次成功连接的AP信息，尝试自动重连
 *
 * 连接策略（两种路径）：
 *   1. 如果扫描缓存中有匹配的AP（通过BSSID匹配），直接用缓存连接
 *   2. 否则，使用NVS保存的SSID/BSSID构造配置进行连接
 *
 * @return esp_err_t
 *   - ESP_OK: 连接成功
 *   - ESP_ERR_NOT_FOUND: NVS中没有保存的WiFi记录
 *   - ESP_FAIL: 连接失败
 */
esp_err_t wifi_auto_connect_last(void)
{
    /* ========== 第1步：初始化WiFi ==========
     * 确保WiFi栈已启动（如果是首次调用会初始化netif、event loop等）
     */
    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;

    /* ========== 第2步：从NVS读取上次保存的AP信息 ==========
     * wifi_load_last_ap() 返回一个结构体，包含：
     *   - ssid:      热点名称（最多32字符）
     *   - bssid:     MAC地址（6字节）
     *   - authmode:  认证模式（WPA2/WPA3等）
     *   - has_bssid: 是否有有效的BSSID
     *   - valid:     整体数据是否有效
     */
    wifi_last_ap_t last = wifi_load_last_ap();
    if (!last.valid) {
        ESP_LOGI(TAG_WIFI, "No last wifi record.");
        return ESP_ERR_NOT_FOUND;  // NVS中没有记录，直接返回
    }

    /* ========== 第3步：尝试从扫描缓存中快速匹配 ==========
     * 如果之前执行过扫描（s.ap_cache_num > 0），且保存的BSSID有效，
     * 则在缓存中查找是否有完全匹配的AP。
     *
     * 好处：使用缓存连接可以利用最新的RSSI、信道等信息，
     *       且复用 wifi_connect_by_index 的逻辑（包括密码复用）
     */
    if (s.ap_cache_num > 0 && last.has_bssid) {
        int idx0 = cache_find_by_bssid(last.bssid);  // 在缓存中查找BSSID
        if (idx0 >= 0) {
            // 用缓存中的索引直接连接（索引+1因为用户侧是1-based）
            ESP_LOGI(TAG_WIFI, "Auto connect (cache): %s", last.ssid);
            return wifi_connect_by_index(idx0 + 1, NULL);
        }
    }

    /* ========== 第4步：缓存未命中，使用保存的配置直接连接 ==========
     * 这种情况发生在：
     *   - 还没扫描过（缓存为空）
     *   - 扫描结果中没有找到相同BSSID的AP（可能AP换了信道/位置）
     *
     * 策略：用NVS保存的信息构造wifi_config，让WiFi栈自己搜索并连接
     */

    // 4.1 先读取当前flash中保存的STA配置（主要是为了复用密码）
    wifi_config_t cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK) memset(&cfg, 0, sizeof(cfg));

    // 4.2 用NVS保存的信息覆盖配置
    strncpy((char*)cfg.sta.ssid, last.ssid, sizeof(cfg.sta.ssid) - 1);  // 设置SSID
    cfg.sta.threshold.authmode = last.authmode;  // 设置最低认证模式要求

    // 4.3 如果有BSSID，锁定到特定AP（避免连到同名其他AP）
    if (last.has_bssid) {
        cfg.sta.bssid_set = 1;
        memcpy(cfg.sta.bssid, last.bssid, 6);
    } else {
        cfg.sta.bssid_set = 0;  // 不锁定BSSID，让WiFi栈自己选择
    }

    /* ========== 第5步：准备事件同步 ==========
     * 清除之前的事件位，重置重试计数器
     */
    xEventGroupClearBits(s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s.retry_num = 0;

    ESP_LOGI(TAG_WIFI, "Auto connect (saved cfg): ssid='%s' bssid_set=%d",
              cfg.sta.ssid, cfg.sta.bssid_set);

    /* ========== 第6步：配置WiFi并启动连接 ==========
     * 设置STA模式 -> 写入配置 -> 断开旧连接 -> 发起新连接
     */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));

    esp_wifi_disconnect();      // 确保先断开（如果有旧连接）
    s.manual_disconnect = false; // 标记：这不是用户手动断开，允许自动重连
    ESP_ERROR_CHECK(esp_wifi_connect());  // 发起连接（非阻塞，结果通过事件通知）

    /* ========== 第7步：等待连接结果 ==========
     * 阻塞等待 WIFI_CONNECTED_BIT 或 WIFI_FAIL_BIT 事件
     * 超时时间：WIFI_CONNECT_TIMEOUT_MS（15秒）
     *
     * xEventGroupWaitBits 参数说明：
     *   - 参数2: 等待的事件位（连接成功或失败）
     *   - 参数3: pdFALSE = 不自动清除位
     *   - 参数4: pdFALSE = 等待任意一个位（不是全部）
     *   - 参数5: 超时时间（ticks）
     */
    EventBits_t bits = xEventGroupWaitBits(
        s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    /* ========== 第8步：处理连接结果 ========== */

    // 8.1 连接成功
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Auto connected OK.");
        wifi_print_info();     // 打印当前连接详情（SSID、IP、MAC等）
        time_sync_init();      // 启动时间同步（SNTP）
        return ESP_OK;
    }

    // 8.2 连接失败（重试次数用尽）
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Auto connect failed.");
        return ESP_FAIL;
    }

    // 8.3 超时（WiFi栈仍在后台重试）
    // 不算失败，因为事件处理器会继续重试
    ESP_LOGI(TAG_WIFI, "Auto connecting... (timeout, keep retry in background)");
    return ESP_OK;
}
esp_err_t wifi_forget_last(bool clear_wifi_flash_cfg)
{
    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;

    // 1) 清除 NVS 命名空间（保存的 last AP）
    {
        nvs_handle_t h;
        err = nvs_open(NVS_NS_WIFI, NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_erase_all(h);
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        } else if (err == ESP_ERR_NVS_NOT_FOUND) {
            // 命名空间不存在 -> 视为已经清空
            err = ESP_OK;
        }
    }
    if (err != ESP_OK) return err;

    // 2) 可选：清空 esp_wifi flash 里保存的 STA ssid/psw
    if (clear_wifi_flash_cfg) {
        wifi_config_t cfg = {0};
        // cfg.sta.ssid/password 全 0 即为空
        esp_err_t e2 = esp_wifi_set_config(WIFI_IF_STA, &cfg);
        if (e2 != ESP_OK) return e2;
    }

    // 3) 断开并禁止自动重连（避免立刻又连上）
    s.manual_disconnect = true;
    s.retry_num = 0;
    esp_wifi_disconnect();

    ESP_LOGI(TAG_WIFI, "Forgot last WiFi record%s.",
              clear_wifi_flash_cfg ? " + cleared flash STA cfg" : "");
    return ESP_OK;
}

esp_err_t wifi_connect_by_ssid(const char *ssid, const char *psw)
{
    if (!ssid || !ssid[0] || !psw) return ESP_ERR_INVALID_ARG;

    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;

    wifi_config_t wifi_config = {0};

    // SSID
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';

    // PSW
    strncpy((char *)wifi_config.sta.password, psw, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    // 不锁 BSSID（让它自己选）
    wifi_config.sta.bssid_set = 0;

    // 阈值：不让它连到 WEP/OPEN（如果你要允许 OPEN，可以按需放开）
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    // 如果你想支持 open（空密码）：
    // if (psw[0] == '\0') wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    xEventGroupClearBits(s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s.retry_num = 0;
    s.last_disconnect_reason = 0;  // 清除上次断开原因

    ESP_LOGI(TAG_WIFI, "Connect by SSID: '%s'", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_disconnect();
    s.manual_disconnect = false;
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(
        s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Connected OK.");
        wifi_print_info();
        time_sync_init();

        // 可选：如果扫描缓存里能找到当前 AP，就保存更准确的 bssid/auth
        wifi_ap_record_t ap = {0};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            wifi_save_last_ap(&ap);
        } else {
            // 至少保证 NVS 里有 ssid/auth（没有 bssid）
            // 这里简单处理：保存不了 bssid 就不强行写
        }

        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Connect failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_WIFI, "Connecting... (timeout, keep retry in background)");
    return ESP_OK;
}
bool wifi_is_connected(void)
{
    return s.connected;
}

esp_err_t wifi_reconnect_saved(void)
{
    esp_err_t err = wifi_init_once();
    if (err != ESP_OK) return err;

    wifi_config_t cfg = {0};
    err = esp_wifi_get_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) return err;

    if (cfg.sta.ssid[0] == '\0') {
        ESP_LOGI(TAG_WIFI, "No saved STA cfg (ssid empty). Use conn/connssid first.");
        return ESP_ERR_NOT_FOUND;
    }

    // 可选：如果你不想锁 BSSID，避免 AP 漫游/更换导致连不上，可以强制清掉：
    // cfg.sta.bssid_set = 0;

    xEventGroupClearBits(s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    s.retry_num = 0;
    s.last_disconnect_reason = 0;  // 清除上次断开原因

    ESP_LOGI(TAG_WIFI, "Reconnecting using saved STA cfg: ssid='%s'%s",
              (char*)cfg.sta.ssid,
              cfg.sta.password[0] ? " (psw set)" : " (psw empty)");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg)); // 确保当前使用的是这份保存配置

    esp_wifi_disconnect();
    s.manual_disconnect = false;
    ESP_ERROR_CHECK(esp_wifi_connect());

    EventBits_t bits = xEventGroupWaitBits(
        s.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Reconnected OK.");
        wifi_print_info();
        time_sync_init();
        return ESP_OK;
    }
    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Reconnect failed.");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_WIFI, "Reconnecting... (timeout, keep retry in background)");
    return ESP_OK;
}

uint16_t wifi_get_scan_cache_count(void)
{
    return s.ap_cache_num;
}

/* -------------------------- bg task -------------------------- */

static void wifi_bg_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));
    wifi_scan_once_and_print_sorted();
    
    wifi_auto_connect_last();
    vTaskDelete(NULL);
}

esp_err_t wifi_start_bg_scan_task(const char *task_name, uint32_t stack_words, UBaseType_t prio)
{
    if (!task_name) task_name = "wifi_bg_task";
    if (stack_words == 0) stack_words = 8192;
    if (prio == 0) prio = 9;

    return (xTaskCreate(wifi_bg_task, task_name, stack_words, NULL, prio, NULL) == pdPASS)
           ? ESP_OK : ESP_FAIL;
}

int wifi_get_last_disconnect_reason(void)
{
    return s.last_disconnect_reason;
}

const char* wifi_disconnect_reason_to_str(int reason)
{
    switch (reason) {
    case 0:
        return "No error";
    case WIFI_REASON_UNSPECIFIED:
        return "Unspecified error";
    case WIFI_REASON_AUTH_EXPIRE:
        return "Authentication expired";
    case WIFI_REASON_AUTH_LEAVE:
        return "Authentication leave";
    case WIFI_REASON_ASSOC_EXPIRE:
        return "Association expired";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "Too many associations";
    case WIFI_REASON_NOT_AUTHED:
        return "Not authenticated";
    case WIFI_REASON_NOT_ASSOCED:
        return "Not associated";
    case WIFI_REASON_ASSOC_LEAVE:
        return "Association leave";
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "Association not authenticated";
    case WIFI_REASON_DISASSOC_PWRCAP_BAD:
        return "Disassociate due to power cap bad";
    case WIFI_REASON_DISASSOC_SUPCHAN_BAD:
        return "Disassociate due to SUP channel bad";
    case WIFI_REASON_IE_INVALID:
        return "Invalid IE";
    case WIFI_REASON_MIC_FAILURE:
        return "MIC failure";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4-way handshake timeout (可能密码错误)";
    case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
        return "Group key update timeout";
    case WIFI_REASON_IE_IN_4WAY_DIFFERS:
        return "IE in 4-way differs";
    case WIFI_REASON_GROUP_CIPHER_INVALID:
        return "Group cipher invalid";
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
        return "Pairwise cipher invalid";
    case WIFI_REASON_AKMP_INVALID:
        return "AKMP invalid";
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
        return "Unsupported RSN IE version";
    case WIFI_REASON_INVALID_RSN_IE_CAP:
        return "Invalid RSN IE capabilities";
    case WIFI_REASON_802_1X_AUTH_FAILED:
        return "802.1X authentication failed";
    case WIFI_REASON_CIPHER_SUITE_REJECTED:
        return "Cipher suite rejected";
    case WIFI_REASON_BEACON_TIMEOUT:
        return "Beacon timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "No AP found";
    case WIFI_REASON_AUTH_FAIL:
        return "Authentication failed";
    case WIFI_REASON_ASSOC_FAIL:
        return "Association failed";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Handshake timeout";
    default:
        if (reason < 0) return "Invalid reason code";
        return "Unknown reason";
    }
}

void wifi_disconnect_manual(void)
{
    s.manual_disconnect = true;
    esp_wifi_disconnect();
}
