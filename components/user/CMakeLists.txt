idf_component_register(
    SRCS "lvgl_api.c"
         "wifi_service.c"
         "mqtt_app.c"
         "time_sync.c"
         "wifi.c"
         "weather.c"
         "console/console_init.c"
         "console/console_wifi.c"
         "console/console_mqtt.c"
         "console/console_system.c"
    INCLUDE_DIRS "." "console"
    REQUIRES driver esp_wifi nvs_flash esp_event esp_netif mqtt esp_http_client console
)
