#ifndef NVS_STORAGE_H
#define NVS_STORAGE_H
#include "esp_err.h"

esp_err_t nvs_storage_init(void);

esp_err_t nvs_storage_save_token(const char* token);

esp_err_t nvs_storage_load_token(char* buffer, size_t buffer_size);

#endif // NVS_MANAGER_H