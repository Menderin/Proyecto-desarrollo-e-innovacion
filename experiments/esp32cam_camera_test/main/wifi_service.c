#include "wifi_service.h"

#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI";
static EventGroupHandle_t s_wifi_events;
static const EventBits_t WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGW(TAG, "Wi-Fi desconectado; reintentando");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi conectado y con direccion IP");
    }
}

esp_err_t wifi_service_init(void)
{
#if !CONFIG_CAMERA_TELEGRAM_ENABLED
    ESP_LOGI(TAG, "Wi-Fi deshabilitado: Telegram no esta configurado");
    return ESP_OK;
#else
    if (strlen(CONFIG_CAMERA_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "SSID vacio; configura Nodo ESP32-CAM en menuconfig");
        return ESP_ERR_INVALID_ARG;
    }

    s_wifi_events = xEventGroupCreate();
    if (s_wifi_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init fallo");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop fallo");
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_config), TAG, "esp_wifi_init fallo");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL),
        TAG,
        "registro WIFI_EVENT fallo"
    );
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL),
        TAG,
        "registro IP_EVENT fallo"
    );

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid,
            CONFIG_CAMERA_WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            CONFIG_CAMERA_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "modo STA fallo");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
        TAG,
        "configuracion STA fallo"
    );
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start fallo");
    ESP_LOGI(TAG, "Conectando a SSID configurado");
    return ESP_OK;
#endif
}

bool wifi_service_wait_connected(TickType_t timeout_ticks)
{
#if !CONFIG_CAMERA_TELEGRAM_ENABLED
    (void)timeout_ticks;
    return false;
#else
    if (s_wifi_events == NULL) {
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout_ticks
    );
    return (bits & WIFI_CONNECTED_BIT) != 0;
#endif
}

esp_err_t wifi_service_sync_time(TickType_t timeout_ticks)
{
#if !CONFIG_CAMERA_TELEGRAM_ENABLED
    (void)timeout_ticks;
    return ESP_ERR_NOT_SUPPORTED;
#else
    time_t now = 0;
    struct tm time_info = {0};
    time(&now);
    gmtime_r(&now, &time_info);
    if (time_info.tm_year >= (2024 - 1900)) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sincronizando hora para validar TLS");
    const esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t result = esp_netif_sntp_init(&config);
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) {
        return result;
    }
    result = esp_netif_sntp_sync_wait(timeout_ticks);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo sincronizar hora SNTP");
    }
    return result;
#endif
}
