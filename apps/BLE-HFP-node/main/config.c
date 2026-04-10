#include "config.h"
#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *BT_HF_TAG = "BT_HF";

void config_init(void)
{
    nvs_init();
    bt_controller_init();
    bt_controller_enable();
}

void nvs_init(void) 
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

void bt_controller_init(void)
{
    esp_err_t ret;
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "initialize controller failed: %s", esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK( ret );
}

void bt_controller_enable(void)
{
    esp_err_t ret;
    const esp_bt_mode_t controller_mode = ESP_BT_MODE_CLASSIC_BT;

    ret = esp_bt_controller_enable(controller_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(BT_HF_TAG, "enable controller failed: %s", esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK( ret );
}
