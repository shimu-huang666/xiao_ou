/**
 * @file console_mqtt.c
 * @brief MQTT 命令注册 (ESP-IDF Console)
 */

#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "mqtt_app.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>  // for strcasecmp

/* ======================== sub 命令 ======================== */
static struct {
    struct arg_str *topic;
    struct arg_int *qos;
    struct arg_end *end;
} sub_args;

static int do_sub(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&sub_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, sub_args.end, argv[0]);
        return 1;
    }

    int qos = (sub_args.qos->count > 0) ? sub_args.qos->ival[0] : 0;
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;

    esp_err_t e = mqtt_app_subscribe_topic(sub_args.topic->sval[0], qos);
    printf("sub topic=%s qos=%d -> %s\n",
           sub_args.topic->sval[0], qos, esp_err_to_name(e));
    return (e == ESP_OK) ? 0 : 1;
}

static void register_sub(void)
{
    sub_args.topic = arg_str1(NULL, NULL, "<topic>", "MQTT topic to subscribe");
    sub_args.qos = arg_int0(NULL, NULL, "<qos>", "QoS level (0-2, default: 0)");
    sub_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "sub",
        .help = "Subscribe to MQTT topic",
        .hint = "<topic> [qos]",
        .func = &do_sub,
        .argtable = &sub_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== unsub 命令 ======================== */
static struct {
    struct arg_str *topic;
    struct arg_end *end;
} unsub_args;

static int do_unsub(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&unsub_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, unsub_args.end, argv[0]);
        return 1;
    }

    esp_err_t e = mqtt_app_unsubscribe_topic(unsub_args.topic->sval[0]);
    printf("unsub topic=%s -> %s\n",
           unsub_args.topic->sval[0], esp_err_to_name(e));
    return (e == ESP_OK) ? 0 : 1;
}

static void register_unsub(void)
{
    unsub_args.topic = arg_str1(NULL, NULL, "<topic>", "MQTT topic to unsubscribe");
    unsub_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "unsub",
        .help = "Unsubscribe from MQTT topic",
        .func = &do_unsub,
        .argtable = &unsub_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== autosub 命令 ======================== */
static struct {
    struct arg_str *mode;
    struct arg_end *end;
} autosub_args;

static int do_autosub(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&autosub_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, autosub_args.end, argv[0]);
        return 1;
    }

    if (autosub_args.mode->count == 0) {
        // 无参数，显示当前状态
        printf("autosub is %s\n", mqtt_app_get_auto_resubscribe() ? "on" : "off");
        printf("Usage: autosub on|off\n");
        return 0;
    }

    const char *m = autosub_args.mode->sval[0];
    if (strcmp(m, "on") == 0 || strcmp(m, "1") == 0) {
        mqtt_app_set_auto_resubscribe(true);
        printf("autosub -> on\n");
    } else if (strcmp(m, "off") == 0 || strcmp(m, "0") == 0) {
        mqtt_app_set_auto_resubscribe(false);
        printf("autosub -> off\n");
    } else {
        printf("Invalid argument. Use: on|off\n");
        return 1;
    }
    return 0;
}

static void register_autosub(void)
{
    autosub_args.mode = arg_str0(NULL, NULL, "<on|off>", "Enable/disable auto-resubscribe");
    autosub_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "autosub",
        .help = "Auto re-subscribe after MQTT reconnect",
        .hint = "[on|off]",
        .func = &do_autosub,
        .argtable = &autosub_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== subs 命令 ======================== */
static int do_subs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mqtt_app_dump_subscriptions();
    return 0;
}

static void register_subs(void)
{
    const esp_console_cmd_t cmd = {
        .command = "subs",
        .help = "List current MQTT subscriptions",
        .func = &do_subs,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== savesubs 命令 ======================== */
static int do_savesubs(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    mqtt_app_save_subscriptions_to_nvs();
    printf("Subscriptions saved to flash\n");
    return 0;
}

static void register_savesubs(void)
{
    const esp_console_cmd_t cmd = {
        .command = "savesubs",
        .help = "Save current subscriptions to NVS (auto-restored on boot)",
        .func = &do_savesubs,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== pub 命令 ======================== */
static struct {
    struct arg_str *topic;
    struct arg_str *payload;
    struct arg_int *qos;
    struct arg_lit *retain;
    struct arg_end *end;
} pub_args;

static int do_pub(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&pub_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, pub_args.end, argv[0]);
        return 1;
    }

    int qos = (pub_args.qos->count > 0) ? pub_args.qos->ival[0] : 0;
    if (qos < 0) qos = 0;
    if (qos > 2) qos = 2;

    bool retain = (pub_args.retain->count > 0);

    int msg_id = mqtt_app_publish_to(
        pub_args.topic->sval[0],
        pub_args.payload->sval[0],
        qos,
        retain
    );

    printf("pub topic=%s qos=%d retain=%d msg_id=%d\n",
           pub_args.topic->sval[0], qos, retain, msg_id);
    return (msg_id >= 0) ? 0 : 1;
}

static void register_pub(void)
{
    pub_args.topic = arg_str1(NULL, NULL, "<topic>", "MQTT topic");
    pub_args.payload = arg_str1(NULL, NULL, "<payload>", "Message payload");
    pub_args.qos = arg_int0("q", "qos", "<0-2>", "QoS level (default: 0)");
    pub_args.retain = arg_lit0("r", "retain", "Set retain flag");
    pub_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "pub",
        .help = "Publish message to MQTT topic",
        .hint = "<topic> <payload>",
        .func = &do_pub,
        .argtable = &pub_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== hb 命令 ======================== */
static struct {
    struct arg_str *mode;
    struct arg_str *subcmd;
    struct arg_end *end;
} hb_args;

static int do_hb(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&hb_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, hb_args.end, argv[0]);
        return 1;
    }

    // hb? - 查询当前状态
    if (hb_args.mode->count == 0 ||
        strcmp(hb_args.mode->sval[0], "?") == 0) {
        const char *topic = mqtt_app_get_hb_topic();
        printf("hb is %s\n", mqtt_app_is_hb_enabled() ? "on" : "off");
        printf("hb topic: %s\n", (topic && topic[0]) ? topic : "(not set)");
        printf("Usage: hb on|off | hb topic [topic] | hb def on|off | hb def?\n");
        return 0;
    }

    const char *m = hb_args.mode->sval[0];

    // hb on/off
    if (strcasecmp(m, "on") == 0 || strcmp(m, "1") == 0) {
        mqtt_app_hb_start();
        printf("hb -> on\n");
        return 0;
    }
    if (strcasecmp(m, "off") == 0 || strcmp(m, "0") == 0) {
        mqtt_app_hb_stop();
        printf("hb -> off\n");
        return 0;
    }

    // hb topic [new_topic]
    if (strcmp(m, "topic") == 0) {
        if (hb_args.subcmd->count == 0) {
            // 查询当前 topic
            const char *cur = mqtt_app_get_hb_topic();
            if (cur && cur[0]) {
                printf("hb topic: %s\n", cur);
            } else {
                printf("hb topic: (not set)\n");
            }
            return 0;
        }
        // 设置新 topic
        const char *new_topic = hb_args.subcmd->sval[0];
        esp_err_t err = mqtt_app_set_hb_topic(new_topic);
        printf("hb topic set -> %s (%s)\n", new_topic, esp_err_to_name(err));
        return (err == ESP_OK) ? 0 : 1;
    }

    // hb def on/off | hb def?
    if (strcmp(m, "def") == 0) {
        if (hb_args.subcmd->count == 0) {
            printf("Usage: hb def on|off\n");
            return 1;
        }
        const char *s = hb_args.subcmd->sval[0];
        if (strcmp(s, "?") == 0) {
            printf("hb default is %s\n", mqtt_app_get_hb_default() ? "on" : "off");
            return 0;
        }
        if (strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) {
            esp_err_t err = mqtt_app_set_hb_default(true);
            printf("hb default -> on (%s)\n", esp_err_to_name(err));
            return (err == ESP_OK) ? 0 : 1;
        }
        if (strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) {
            esp_err_t err = mqtt_app_set_hb_default(false);
            printf("hb default -> off (%s)\n", esp_err_to_name(err));
            return (err == ESP_OK) ? 0 : 1;
        }
        printf("Usage: hb def on|off\n");
        return 1;
    }

    printf("Usage: hb on|off | hb topic [topic] | hb def on|off | hb def?\n");
    return 1;
}

static void register_hb(void)
{
    hb_args.mode = arg_str0(NULL, NULL, "<on|off|def|topic>", "Heartbeat mode");
    hb_args.subcmd = arg_str0(NULL, NULL, "<on|off|topic|?>", "Sub-command");
    hb_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "hb",
        .help = "MQTT heartbeat control",
        .hint = "[on|off|topic [topic]|def on|off|def?]",
        .func = &do_hb,
        .argtable = &hb_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== mqttsend (legacy) 命令 ======================== */
static struct {
    struct arg_str *message;
    struct arg_end *end;
} mqttsend_args;

static int do_mqttsend(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&mqttsend_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, mqttsend_args.end, argv[0]);
        return 1;
    }

    if (mqttsend_args.message->count == 0) {
        printf("Usage: mqttsend <message>\n");
        return 1;
    }

    const char *MQTT_SUB_TOPIC = "/shimu_test";
    int ret = mqtt_app_publish(MQTT_SUB_TOPIC,
                               mqttsend_args.message->sval[0],
                               0, 1);
    if (ret < 0) {
        printf("mqttsend failed\n");
        return 1;
    }
    printf("mqttsend ok\n");
    return 0;
}

static void register_mqttsend(void)
{
    mqttsend_args.message = arg_str1(NULL, NULL, "<message>", "Message to send");
    mqttsend_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "mqttsend",
        .help = "Publish message to default topic (legacy)",
        .func = &do_mqttsend,
        .argtable = &mqttsend_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== 注册所有 MQTT 命令 ======================== */
void register_mqtt_cmds(void)
{
    register_sub();
    register_unsub();
    register_autosub();
    register_subs();
    register_savesubs();
    register_pub();
    register_hb();
    register_mqttsend();
}
