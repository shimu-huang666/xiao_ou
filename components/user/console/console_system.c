/**
 * @file console_system.c
 * @brief 系统命令注册 (ESP-IDF Console)
 */

#include "esp_console.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <inttypes.h>

#include "weather.h"

// 时间函数（如果没有 time_sync.h 就保持 extern）
extern bool time_is_valid(void);
extern void print_time_now(void);

/* ======================== time 命令 ======================== */
static int do_time(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!time_is_valid()) {
        printf("SNTP not synced yet. Wait a few seconds after WiFi connect.\n");
        return 1;
    }
    print_time_now();
    return 0;
}

static void register_time(void)
{
    const esp_console_cmd_t cmd = {
        .command = "time",
        .help = "Show current time (synced via SNTP)",
        .func = &do_time,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== weather 命令 ======================== */
static int do_weather(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    weather_request();
    printf("Weather requested (result will print when done)\n");
    return 0;
}

static void register_weather(void)
{
    const esp_console_cmd_t cmd = {
        .command = "weather",
        .help = "Get weather by WiFi IP geolocation (Open-Meteo API)",
        .func = &do_weather,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== reboot 命令 ======================== */
static int do_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("Rebooting...\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;  // 不会执行到这里
}

static void register_reboot(void)
{
    const esp_console_cmd_t cmd = {
        .command = "reboot",
        .help = "Reboot the device",
        .func = &do_reboot,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== free 命令 ======================== */
static int do_free(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free = esp_get_minimum_free_heap_size();

    printf("Free heap: %" PRIu32 " bytes\n", free_heap);
    printf("Min free:  %" PRIu32 " bytes\n", min_free);
    return 0;
}

static void register_free(void)
{
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Show free heap memory",
        .func = &do_free,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== tasks 命令 ======================== */
/**
 * 使用 uxTaskGetSystemState() 获取任务列表
 * 返回每个任务的名称、状态、优先级、剩余栈空间
 */
static int do_tasks(int argc, char **argv)
{
    (void)argc;
    (void)argv;

#if CONFIG_FREERTOS_USE_TRACE_FACILITY
    // 获取任务数量
    UBaseType_t task_num = uxTaskGetNumberOfTasks();
    if (task_num == 0) {
        printf("No tasks found\n");
        return 0;
    }

    // 分配 TaskStatus_t 数组 (在栈上分配，避免动态内存)
    TaskStatus_t *task_array = malloc(task_num * sizeof(TaskStatus_t));
    if (task_array == NULL) {
        printf("Failed to allocate memory for task list\n");
        return 1;
    }

    uint32_t total_runtime;
    UBaseType_t actual_count = uxTaskGetSystemState(task_array, task_num, &total_runtime);

    printf("Name            State  Prio  Stack\n");
    printf("------------------------------------\n");

    for (UBaseType_t i = 0; i < actual_count; i++) {
        const char *state_str;
        switch (task_array[i].eCurrentState) {
            case eRunning:   state_str = "Run"; break;
            case eReady:     state_str = "Rdy"; break;
            case eBlocked:   state_str = "Blk"; break;
            case eSuspended: state_str = "Sus"; break;
            case eDeleted:   state_str = "Del"; break;
            default:         state_str = "???"; break;
        }

        printf("%-16s %-3s   %-3u   %-5u\n",
               task_array[i].pcTaskName,
               state_str,
               (unsigned int)task_array[i].uxCurrentPriority,
               (unsigned int)task_array[i].usStackHighWaterMark);
    }

    free(task_array);
    return 0;
#else
    printf("tasks command requires CONFIG_FREERTOS_USE_TRACE_FACILITY=y\n");
    printf("Current task count: %u\n", (unsigned int)uxTaskGetNumberOfTasks());
    return 0;
#endif
}

static void register_tasks(void)
{
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "List FreeRTOS tasks (name, state, priority, stack high water)",
        .func = &do_tasks,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== task_count 命令 ======================== */
static int do_task_count(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UBaseType_t count = uxTaskGetNumberOfTasks();
    printf("Number of tasks: %u\n", (unsigned int)count);
    return 0;
}

static void register_task_count(void)
{
    const esp_console_cmd_t cmd = {
        .command = "taskcount",
        .help = "Show number of FreeRTOS tasks",
        .func = &do_task_count,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/* ======================== 注册所有系统命令 ======================== */
void register_system_cmds(void)
{
    register_time();
    register_weather();
    register_reboot();
    register_free();
    register_tasks();
    register_task_count();
}
