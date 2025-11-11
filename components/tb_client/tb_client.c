#include "tb_client.h"
#include "nvs_storage.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "TB_CLIENT";


const char *THINGSBOARD_SERVER = "demo.thingsboard.io";
const uint16_t PORT = 1883U;
// ThingsBoard server configuration ends

// Provisioning device info
const char *DEVICE_NAME = "ESP32_DEVICE_004";
const char *PROVISION_DEVICE_KEY = "bxplopnphlyifce1t5hu";
const char *PROVISION_DEVICE_SECRET = "nehbgo7f824oj2vztjew";

const char *PROV_RESPONSE_TOPIC = "/provision/response";
const char *PROV_REQUEST_TOPIC = "/provision/request";
const char *CLAIM_REQUEST_TOPIC = "v1/devices/me/claim";

struct tb_client {
    tb_client_config_t              config;
    esp_mqtt_client_handle_t        mqtt_client;
    char                            device_token[128];
    bool                            is_provisioned;
    bool                            is_connected;
    bool                            is_claimed;
    SemaphoreHandle_t               mutex;
};

static void tb_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
static void handle_rpc_request(tb_client_handle_t handle, const char* topic, int topic_len, const char* data, int data_len);
static void handle_provisioning_response(tb_client_handle_t handle, const char* data, int data_len);
static void tb_provisioning_request(tb_client_handle_t handle);
static void tb_client_restart_task(void* param);
static void tb_claiming_request(tb_client_handle_t handle); 

static inline bool topic_eq(const char* t, int len, const char* lit) 
{
    size_t L = strlen(lit);
    return (len == (int)L) && (memcmp(t, lit, L) == 0);
}

static inline bool topic_startwith(const char* t, int len, const char* pre) 
{
    size_t L = strlen(pre);
    return len >= (int)L && memcmp(t, pre, L) == 0;
}

static bool topic_last_segment(const char* t, int len, char* out, size_t out_sz) 
{
    int i = len - 1;
    while (i >= 0 && t[i] != '/') i--;
    int off = i + 1, n = len - off;
    if (n <= 0 || (size_t)n >= out_sz) return false;
    memcpy(out, t + off, n); out[n] = '\0';
    return true;
}

tb_client_handle_t tb_client_init(const tb_client_config_t* config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }

    tb_client_handle_t handle = malloc(sizeof(struct tb_client));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for tb_client");
        return NULL;
    }
    memset(handle, 0, sizeof(struct tb_client));

    handle->config = *config;
    handle->mutex = xSemaphoreCreateMutex();
    if (handle->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(handle);
        return NULL;
    }

    // Initialize MQTT client here...
    ESP_LOGI(TAG, "TB Client created");
    return handle;
}

esp_err_t tb_client_destroy(tb_client_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->mqtt_client) {
        esp_mqtt_client_stop(handle->mqtt_client);
        esp_mqtt_client_destroy(handle->mqtt_client);
    }

    if (handle->mutex) {
        vSemaphoreDelete(handle->mutex);
    }

    free(handle);
    ESP_LOGI(TAG, "TB Client destroyed");
    return ESP_OK;
}

esp_err_t tb_client_start(tb_client_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Start MQTT client connection here...
    handle->is_provisioned = (nvs_storage_load_token("device_token", handle->device_token, sizeof(handle->device_token)) == ESP_OK);
    
    char claim_buffer[1];
    if (nvs_storage_load_token("is_claimed", claim_buffer, sizeof(claim_buffer)) == ESP_OK)
    {
        handle->is_claimed = strncmp(claim_buffer, "1", sizeof(claim_buffer)) == 0;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = THINGSBOARD_SERVER,
        .broker.address.port     = PORT,
        .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
        .session.protocol_ver    = MQTT_PROTOCOL_V_3_1_1,
        .credentials.username    = handle->is_provisioned ? handle->device_token : "provision",
        .session.keepalive       = 60,
        .network.reconnect_timeout_ms = 2000,
        .network.disable_auto_reconnect = false,
    };
    if (handle->is_provisioned) {
        mqtt_cfg.credentials.username = handle->device_token;
        ESP_LOGI(TAG, "Using token from NVS: %s", handle->device_token);
    } else {
        mqtt_cfg.credentials.username = "provision";
        ESP_LOGW(TAG, "No token -> fallback to provision");
    }

    handle->mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(handle->mqtt_client, ESP_EVENT_ANY_ID, tb_mqtt_event_handler, handle);

    return esp_mqtt_client_start(handle->mqtt_client);
}

void tb_client_send_telemetry(tb_client_handle_t handle, const char* payload) {
    if (handle == NULL || payload == NULL) 
    {
        ESP_LOGE(TAG, "Invalid argument to send telemetry");
        return;
    }

    if (!handle->is_connected) 
    {
        ESP_LOGW(TAG, "Client not connected, cannot send telemetry");
        return;
    }

    if (xSemaphoreTake(handle->mutex, pdMS_TO_TICKS(1000)) == pdTRUE) 
    {
        esp_mqtt_client_publish(handle->mqtt_client, "v1/devices/me/telemetry", payload, 0, 1, 0);
        ESP_LOGI(TAG, "Telemetry sent: %s", payload);
        xSemaphoreGive(handle->mutex);
    }

    else 
    {
        ESP_LOGE(TAG, "Failed to take mutex to send telemetry");
    }   
}

bool tb_client_is_connected(tb_client_handle_t handle) {
    if (handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument to check connection status");
        return false;
    }

    if (handle->is_provisioned == false) {
        ESP_LOGW(TAG, "Client not provisioned");
        return false;
    }
    
    if (handle->is_connected == false) {
        ESP_LOGW(TAG, "Client not connected");
        return false;
    }

    return true;
}

static void tb_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    tb_client_handle_t handle = (tb_client_handle_t)handler_args;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    if (handle == NULL || event == NULL) {
        ESP_LOGE(TAG, "Invalid event handler arguments");
        return;
    }

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            handle->is_connected = true;
            if (!handle->is_provisioned) 
            {
                ESP_LOGI(TAG, "Starting device provisioning");
                esp_mqtt_client_subscribe(handle->mqtt_client, PROV_RESPONSE_TOPIC, 1);
                tb_provisioning_request(handle);
            }

            else 
            {
                esp_mqtt_client_subscribe(handle->mqtt_client, "v1/devices/me/rpc/request/+", 1);
                esp_mqtt_client_subscribe(handle->mqtt_client, "v1/devices/me/attributes/response/+", 1);
                esp_mqtt_client_subscribe(handle->mqtt_client, "v1/devices/me/attributes/+", 1);
                esp_mqtt_client_subscribe(handle->mqtt_client, "v2/fw/response/+", 1);
                
                if (!handle->is_claimed) {
                    ESP_LOGI(TAG, "Starting device claiming");
                    tb_claiming_request(handle);
                    handle->is_claimed = true;
                    nvs_storage_save_token("is_claimed", "1");
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            handle->is_connected = false;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received on topic: %.*s", event->topic_len, event->topic);
            if (topic_startwith(event->topic, event->topic_len, "v1/devices/me/rpc/request/")) {
                handle_rpc_request(handle, event->topic, event->topic_len, event->data, event->data_len);
            } else if (topic_eq(event->topic, event->topic_len, PROV_RESPONSE_TOPIC)) { // Dùng hằng số
                handle_provisioning_response(handle, event->data, event->data_len);
            }
            break;

        default:
            ESP_LOGI(TAG, "Unhandled MQTT event id: %d", event_id);
            break;
    }
}

static void handle_provisioning_response(tb_client_handle_t handle, const char* data, int data_len) {
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse provisioning response JSON");
        return; 
    }

    const cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *credentialsType = cJSON_GetObjectItemCaseSensitive(root, "credentialsType");
    const cJSON *credentialsValue = cJSON_GetObjectItemCaseSensitive(root, "credentialsValue");


    bool is_successed = (cJSON_IsString(status) && strcmp(status->valuestring, "SUCCESS") == 0);
    if (!is_successed)
    {
        ESP_LOGE(TAG, "Provision FAILED: %.*s", data_len, data);
        cJSON_Delete(root);
        return;
    }

    if (cJSON_IsString(credentialsType) && cJSON_IsString(credentialsValue) 
        && strcmp(credentialsType->valuestring, "ACCESS_TOKEN") == 0) 
    {
        ESP_LOGI(TAG, "Provision OK. ");
        
        strncpy(handle->device_token, credentialsValue->valuestring, sizeof(handle->device_token)-1);
        handle->device_token[sizeof(handle->device_token)-1] = '\0';

        if (nvs_storage_save_token("device_token", handle->device_token) == ESP_OK)
        {
            // Save the token
            ESP_LOGI(TAG, "Save to the NVS. Restart MQTT...");
            xTaskCreate(tb_client_restart_task, "mqtt_restart", 4096, handle, tskIDLE_PRIORITY, NULL);
        }
        
        else 
        {
            ESP_LOGE(TAG, "Failed to save token");
        }
    }

    cJSON_Delete(root);
}

static void handle_rpc_request(tb_client_handle_t handle, const char* topic, int topic_len, const char* data, int data_len) 
{
    char req_id[16];
    if (!topic_last_segment(topic, topic_len, req_id, sizeof(req_id)))
    {
        ESP_LOGE(TAG, "Bad RPC topic, can not get req_id");
        return;
    }

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root)
    {
        ESP_LOGE(TAG, "RPC JSON parse fail");
        return;
    }

    const cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    if (!cJSON_IsString(method))
    {
        cJSON_Delete(root);
        return;
    }

    ESP_LOGE(TAG, "RPC method: %s", method->valuestring);

    bool rpc_handled = false;
    cJSON *res = cJSON_CreateObject();


    // if (strcmp(method->valuestring, "setLightState") == 0 && cJSON_IsObject(params))
    // {
    //     const cJSON *power_json = cJSON_GetObjectItemCaseSensitive(params, "power");
    //     const cJSON *bright_json = cJSON_GetObjectItemCaseSensitive(params, "brightness");
        
    //     bool power = cJSON_IsBool(power_json) ? cJSON_IsTrue(power_json) : false;
    //     float brightness = cJSON_IsNumber(bright_json) ? (float)bright_json->valueint : 0.0f;

    //     // ** GỌI CALLBACK **
    //     if (handle->config.callbacks && handle->config.callbacks->on_rpc_set_light) {
    //         handle->config.callbacks->on_rpc_set_light(handle->config.callbacks->context, power, brightness);
    //     }
        
    //     cJSON_AddBoolToObject(resp, "success", true);
    //     rpc_handled = true;
    // }

    // if (strcmp(method->valuestring, "fwUpdate") == 0 && cJSON_IsObject(params))
    // {
    //     const cJSON *url = cJSON_GetObjectItemCaseSensitive(params, "url");
    //     if (cJSON_IsString(url) && url->valuestring != NULL) 
    //     {
    //         if (handle->config.callbacks && handle->config.callbacks->on_rpc_ota_update) {
    //             handle->config.callbacks->on_rpc_ota_update(handle->config.callbacks->context, url->valuestring);
    //         }
    //         cJSON_AddBoolToObject(res, "success", true);
    //     } else {
    //          cJSON_AddBoolToObject(res, "success", false);
    //     }
    //     rpc_handled = true;
    // }
    
    // if (rpc_handled) {
    //     char resp_topic[128];
    //     snprintf(resp_topic, sizeof(resp_topic), TB_TOPIC_RPC_RESPONSE, req_id);
        
    //     char* resp_str = cJSON_PrintUnformatted(res);
    //     esp_mqtt_client_publish(handle->mqtt_client, resp_topic, resp_str, 0, 1, 0);
    //     cJSON_free(resp_str);
    // }
    
    // cJSON_Delete(res);
    // cJSON_Delete(root);
}


static void tb_claiming_request(tb_client_handle_t handle) 
{
    cJSON *root = cJSON_CreateObject();
    if (!root) 
    {
        ESP_LOGE(TAG, "cJSON create object fail");
        return;
    }

    cJSON_AddStringToObject(root, "secretKey", "abcd1234");
    cJSON_AddNumberToObject(root, "durationMs", 60000);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) 
    { 
        ESP_LOGE(TAG, "cJSON print fail"); 
        return; 
    }

    ESP_LOGI(TAG, "Send request claim: %s", json);
    esp_mqtt_client_publish(handle->mqtt_client, CLAIM_REQUEST_TOPIC, json, 0, 1, 0);
    cJSON_free(json);
}

static void tb_provisioning_request(tb_client_handle_t handle) 
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "deviceName",           handle->config.device_name);
    cJSON_AddStringToObject(root, "provisionDeviceKey",   handle->config.provision_key);
    cJSON_AddStringToObject(root, "provisionDeviceSecret",handle->config.provision_secret);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) 
    { 
        ESP_LOGE(TAG, "cJSON print fail"); 
        return; 
    }

    ESP_LOGI(TAG, "Send request provision: %s", json);
    esp_mqtt_client_publish(handle->mqtt_client, PROV_REQUEST_TOPIC, json, 0, 1, 0);
    cJSON_free(json);
}

static void tb_client_restart_task(void *param)
{
    tb_client_handle_t handle = (tb_client_handle_t) param;

    ESP_LOGI(TAG, "Restarting to MQTT with new TOKEN");
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (xSemaphoreTake(handle->mutex, portMAX_DELAY) == pdTRUE)
    {
        handle->is_connected = false;
        if (handle->mqtt_client)
        {
            esp_mqtt_client_stop(handle->mqtt_client);
            esp_mqtt_client_destroy(handle->mqtt_client);
        }

        handle->is_provisioned = true;
        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.hostname = THINGSBOARD_SERVER,
            .broker.address.port     = PORT,
            .broker.address.transport = MQTT_TRANSPORT_OVER_TCP,
            .session.protocol_ver    = MQTT_PROTOCOL_V_3_1_1,
            .credentials.username    = handle->device_token,
            .session.keepalive       = 60,
            .network.reconnect_timeout_ms = 2000,
            .network.disable_auto_reconnect = false,
        };

        handle->mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        esp_mqtt_client_register_event(handle->mqtt_client, ESP_EVENT_ANY_ID, tb_mqtt_event_handler, handle);

        esp_mqtt_client_start(handle->mqtt_client);
        xSemaphoreGive(handle->mutex);
    }

    vTaskDelete(NULL);
}

