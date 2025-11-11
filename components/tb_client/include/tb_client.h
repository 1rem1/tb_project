#ifndef TB_CLIENT_H
#define TB_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>

typedef struct tb_client* tb_client_handle_t; 

typedef struct {
    // void *callback;
} tb_client_callbacks_t;


typedef struct {
    const char* server_url;
    const char* provision_key;
    const char* provision_secret;
    const char* device_name;
    const tb_client_callbacks_t* callbacks;
} tb_client_config_t;


tb_client_handle_t tb_client_init(const tb_client_config_t* config);

esp_err_t tb_client_destroy(tb_client_handle_t handle);
esp_err_t tb_client_start(tb_client_handle_t handle);

void tb_client_send_telemetry(tb_client_handle_t handle, const char* payload);

bool tb_client_is_connected(tb_client_handle_t handle);

#endif // TB_CLIENT_H