#include "nvs_storage.h"
#include "nvs_flash.h"

#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_log.h"

static const char* TAG = "NVS_STORAGE";

static SemaphoreHandle_t s_nvs_mutex = NULL;

static bool s_initialized = {0};

esp_err_t nvs_storage_init(void)
{
    // Create mutex for NVS operations
    if (s_nvs_mutex == NULL) 
    {
        s_nvs_mutex = xSemaphoreCreateMutex();
        if (s_nvs_mutex == NULL) 
        {
            ESP_LOGE(TAG, "Failed to create NVS mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Initialize NVS only once
    if (s_initialized) 
    {
        return ESP_OK;
    }

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    s_initialized = (err == ESP_OK);
    return err;
}


esp_err_t nvs_storage_save_token(const char* key, const char* value)
{
    if (!s_initialized || !s_nvs_mutex) {
        ESP_LOGE(TAG, "NVS storage not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;
    nvs_handle_t handle;
    
    if (xSemaphoreTake(s_nvs_mutex, portMAX_DELAY) == pdTRUE)
    {
        // "storage" là tên "ngăn kéo" (namespace)
        err = nvs_open("storage", NVS_READWRITE, &handle); 
        if (err == ESP_OK)
        {
            // *** ĐÂY LÀ THAY ĐỔI: Dùng 'key' thay vì "creValue" ***
            err = nvs_set_str(handle, key, value); 
            if (err == ESP_OK)
            {
                err = nvs_commit(handle);
            }
            nvs_close(handle);
        }
        xSemaphoreGive(s_nvs_mutex);
        return err;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take NVS mutex");
        return ESP_FAIL;  
    }
}

esp_err_t nvs_storage_load_token(const char* key, char* out_buffer, size_t buffer_size)
{
    if (!s_initialized || !s_nvs_mutex) {
        ESP_LOGE(TAG, "NVS storage not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t err;
    nvs_handle_t handle;

    if (xSemaphoreTake(s_nvs_mutex, portMAX_DELAY) == pdTRUE)
    {
        err = nvs_open("storage", NVS_READONLY, &handle);
        if (err == ESP_OK)
        {
            size_t required_size = 0;
            err = nvs_get_str(handle, key, NULL, &required_size); 
            if (err == ESP_OK && required_size <= buffer_size)
            {
                err = nvs_get_str(handle, key, out_buffer, &required_size);
            }
            else if (err == ESP_OK)
            {
                err = ESP_ERR_NVS_INVALID_LENGTH;
            }
            nvs_close(handle);
        }
        xSemaphoreGive(s_nvs_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take NVS mutex");
        err = ESP_FAIL;
    }

    return err;
}