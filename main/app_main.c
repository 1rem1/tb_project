#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "esp_netif.h"
#include "esp_event.h"
#include <inttypes.h>

#include "nvs_storage.h"
#include "tb_client.h"
#include "wifi_prov_mgr.h"

#include "cJSON.h"


static const char* TAG = "APP";

static tb_client_handle_t g_tb_client = NULL;

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free heap: %" PRIu32, esp_get_free_heap_size());
    // ESP_LOGI(TAG, "[APP] Firmware: %s", FW_VERSION);

    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_prov_start();


    char device_name[12];
    nvs_storage_load_token("device_name", device_name, sizeof(device_name));
    tb_client_config_t tb_config = {
        .device_name = device_name,
        .server_url = "demo.thingsboard.io:1883",
        .provision_key = "bxplopnphlyifce1t5hu",
        .provision_secret = "nehbgo7f824oj2vztjew",
    };

    g_tb_client = tb_client_init(&tb_config);

    if (g_tb_client) {
        ESP_ERROR_CHECK(tb_client_start(g_tb_client));
    }

   while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        if (tb_client_is_connected(g_tb_client)) {
            
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "temperature", 55.5); 
            char *payload = cJSON_PrintUnformatted(root);
            
            tb_client_send_telemetry(g_tb_client, payload);
            
            cJSON_free(payload);
            cJSON_Delete(root);
        }
    }
}
