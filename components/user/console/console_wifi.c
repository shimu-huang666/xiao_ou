/**
 * @file console_wifi.c
 * @brief WiFi 命令注册 (ESP-IDF Console)
 */

#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "wifi.h"
#include "wifi_service.h"
#include <string.h>

/* ======================== scan 命令 ======================== */
static int do_scan(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    wifi_req_t r = {.type = WIFI_REQ_SCAN};
    esp_err_t e = wifi_service_post(&r);

    if (e == ESP_OK) {
        printf("Scan requested (result will print when done)\n");
    } else {
        printf("Scan failed: %s\n", esp_err_to_name(e));
    }
    return (e == ESP_OK) ? 0 : 1;
}

static void register_scan(void)
{
    const esp_console_cmd_t cmd = {
        .command = "scan",
        .help = "Scan WiFi networks (async, sorted by RSSI)",
        .func = &do_scan,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== conn 命令 ======================== */
static struct {
    struct arg_int *index;
    struct arg_str *password;
    struct arg_end *end;
} conn_args;

static int do_conn(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&conn_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, conn_args.end, argv[0]);
        return 1;
    }

    wifi_req_t r = {.type = WIFI_REQ_CONN_INDEX};
    r.u.conn_index.index = conn_args.index->ival[0];
    r.u.conn_index.has_psw = (conn_args.password->count > 0);

    if (r.u.conn_index.has_psw) {
        strncpy(r.u.conn_index.psw,
                conn_args.password->sval[0],
                sizeof(r.u.conn_index.psw) - 1);
        r.u.conn_index.psw[sizeof(r.u.conn_index.psw) - 1] = '\0';
    }

    esp_err_t e = wifi_service_post(&r);
    if (e == ESP_OK) {
        printf("Connect requested (index=%d)\n", r.u.conn_index.index);
    } else {
        printf("Connect failed: %s\n", esp_err_to_name(e));
    }
    return (e == ESP_OK) ? 0 : 1;
}

static void register_conn(void)
{
    conn_args.index = arg_int1(NULL, NULL, "<index>", "AP index from scan result (1-based)");
    conn_args.password = arg_str0(NULL, NULL, "<password>", "WiFi password (optional)");
    conn_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "conn",
        .help = "Connect to WiFi AP by scan index",
        .hint = "<index> [password]",
        .func = &do_conn,
        .argtable = &conn_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== connssid 命令 ======================== */
static struct {
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} connssid_args;

static int do_connssid(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&connssid_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, connssid_args.end, argv[0]);
        return 1;
    }

    wifi_req_t r = {.type = WIFI_REQ_CONN_SSID};
    strncpy(r.u.conn_ssid.ssid,
            connssid_args.ssid->sval[0],
            sizeof(r.u.conn_ssid.ssid) - 1);
    strncpy(r.u.conn_ssid.psw,
            connssid_args.password->sval[0],
            sizeof(r.u.conn_ssid.psw) - 1);

    esp_err_t e = wifi_service_post(&r);
    if (e == ESP_OK) {
        printf("Connect requested (ssid=%s)\n", r.u.conn_ssid.ssid);
    } else {
        printf("Connect failed: %s\n", esp_err_to_name(e));
    }
    return (e == ESP_OK) ? 0 : 1;
}

static void register_connssid(void)
{
    connssid_args.ssid = arg_str1(NULL, NULL, "<ssid>", "WiFi SSID");
    connssid_args.password = arg_str1(NULL, NULL, "<password>", "WiFi password");
    connssid_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "connssid",
        .help = "Connect to WiFi by SSID and password",
        .hint = "<ssid> <password>",
        .func = &do_connssid,
        .argtable = &connssid_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== disconn 命令 ======================== */
static int do_disconn(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    wifi_req_t r = {.type = WIFI_REQ_DISCONN};
    esp_err_t e = wifi_service_post(&r);

    if (e == ESP_OK) {
        printf("Disconnect requested\n");
    } else {
        printf("Disconnect failed: %s\n", esp_err_to_name(e));
    }
    return (e == ESP_OK) ? 0 : 1;
}

static void register_disconn(void)
{
    const esp_console_cmd_t cmd = {
        .command = "disconn",
        .help = "Disconnect from WiFi (manual, no auto-reconnect)",
        .func = &do_disconn,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== reconn 命令 ======================== */
static int do_reconn(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t e = wifi_reconnect_saved();
    if (e != ESP_OK) {
        printf("Reconnect failed: %s\n", esp_err_to_name(e));
        return 1;
    }
    return 0;
}

static void register_reconn(void)
{
    const esp_console_cmd_t cmd = {
        .command = "reconn",
        .help = "Reconnect using saved WiFi config from flash",
        .func = &do_reconn,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== info 命令 ======================== */
static int do_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    wifi_print_info();
    return 0;
}

static void register_info(void)
{
    const esp_console_cmd_t cmd = {
        .command = "info",
        .help = "Show current WiFi connection info (SSID, IP, RSSI, etc.)",
        .func = &do_info,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== forget 命令 ======================== */
static int do_forget(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    esp_err_t e = wifi_forget_last(true);
    if (e != ESP_OK) {
        printf("Forget failed: %s\n", esp_err_to_name(e));
        return 1;
    }
    printf("WiFi credentials cleared\n");
    return 0;
}

static void register_forget(void)
{
    const esp_console_cmd_t cmd = {
        .command = "forget",
        .help = "Erase saved WiFi credentials and disconnect",
        .func = &do_forget,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== mem 命令 ======================== */
static int do_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    wifi_print_memory();
    return 0;
}

static void register_mem(void)
{
    const esp_console_cmd_t cmd = {
        .command = "mem",
        .help = "Show saved WiFi memory (NVS + STA flash config)",
        .func = &do_mem,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== 注册所有 WiFi 命令 ======================== */
void register_wifi_cmds(void)
{
    register_scan();
    register_conn();
    register_connssid();
    register_disconn();
    register_reconn();
    register_info();
    register_forget();
    register_mem();
}
