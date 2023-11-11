/* *****************************************************************************
 * File:   drv_esptouch.c
 * Author: Dimitar Lilov
 *
 * Created on 2022 06 18
 * 
 * Description: ...
 * 
 **************************************************************************** */

/* *****************************************************************************
 * Header Includes
 **************************************************************************** */
#include "drv_esptouch.h"
#if CONFIG_USE_WIFI

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
//#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

#include "drv_wifi_if.h"

/* *****************************************************************************
 * Configuration Definitions
 **************************************************************************** */
#define TAG "drv_esptouch"

/* *****************************************************************************
 * Constants and Macros Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Enumeration Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Type Definitions
 **************************************************************************** */

/* *****************************************************************************
 * Function-Like Macros
 **************************************************************************** */

/* *****************************************************************************
 * Variables Definitions
 **************************************************************************** */
/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t s_esptouch_event_group;

TaskHandle_t p_esptouch_handle = NULL;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;

//SemaphoreHandle_t b_esptouch_starting = NULL;

/* *****************************************************************************
 * Prototype of functions definitions
 **************************************************************************** */
static void smartconfig_example_task(void * parm);

/* *****************************************************************************
 * Functions
 **************************************************************************** */

static void smartconfig_example_task(void * parm)
{
    bool b_working = false;
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    b_working = true;
    while (b_working) 
    {
        uxBits = xEventGroupWaitBits(s_esptouch_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) 
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) 
        {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            b_working = false;
            
        }
    }
    p_esptouch_handle = NULL;
    vTaskDelete(NULL);
}

static void esptouch_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) 
    {
        ESP_LOGI(TAG, "Scan done");
    } 
    else 
    if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) 
    {
        ESP_LOGI(TAG, "Found channel");
    } 
    else 
    if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) 
    {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };
        uint8_t rvd_data[33] = { 0 };

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        if (evt->type == SC_TYPE_ESPTOUCH_V2) 
        {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        drv_wifi_sta_ssid_pass_set((char*)ssid, (char*)password, evt->bssid_set, evt->bssid);
    } 
    else 
    if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) 
    {
        drv_esptouch_done();
    }

}

void drv_esptouch_init(void)
{
    ESP_LOGW(TAG, "s_esptouch_event_group:create");
    s_esptouch_event_group = xEventGroupCreate();
    ESP_LOGW(TAG, "s_esptouch_event_group:create end");
    drv_esptouch_init_event_handler();

    // if (b_esptouch_starting == NULL)
    // {
    //     b_esptouch_starting = xSemaphoreCreateBinary();
    //     xSemaphoreGive(b_esptouch_starting);
    // }

}

void drv_esptouch_init_event_handler(void)
{
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &esptouch_event_handler, NULL) );
}

bool drv_esptouch_is_started(void)
{
    return (p_esptouch_handle != NULL);
}

void drv_esptouch_start(void)
{
    //xSemaphoreTake(b_esptouch_starting, portMAX_DELAY);


    if (p_esptouch_handle == NULL)
    {
        ESP_LOGW(TAG, "drv_esptouch task start begin");
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 8192, NULL, 3, &p_esptouch_handle);
        ESP_LOGW(TAG, "drv_esptouch task start end");
    }
    else
    {
        ESP_LOGE(TAG, "drv_esptouch task already started");
    }
    //xSemaphoreGive(b_esptouch_starting);
}

void drv_esptouch_disconnected(void)
{
    ESP_LOGW(TAG, "s_esptouch_event_group:disconnected");
    xEventGroupClearBits(s_esptouch_event_group, CONNECTED_BIT);
    ESP_LOGW(TAG, "s_esptouch_event_group:disconnected end");
}

void drv_esptouch_connected(void)
{
    ESP_LOGW(TAG, "s_esptouch_event_group:connected");
    xEventGroupSetBits(s_esptouch_event_group, CONNECTED_BIT);
    ESP_LOGW(TAG, "s_esptouch_event_group:connected end");
}

void drv_esptouch_done(void)
{
    xEventGroupSetBits(s_esptouch_event_group, ESPTOUCH_DONE_BIT);
}
#endif //#if CONFIG_USE_WIFI