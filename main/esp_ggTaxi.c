/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_log.h"



/* My include files */
#include "common.h"
#include "gg_https.h"
#include "gg_wss.h"

#include "ws2812.h"
#include "display.h"

#include "handlers.h"

/* used for display_task -> main_task */
QueueHandle_t button_queue;

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
/* The event group allows multiple bits for each event,
 * but we only care about one event
 * - are we connected to the AP with an IP? */
const int WIFI_CONNECTED_BIT = BIT0;


static const char *WIFI_TAG = "WIFI";
static int s_retry_num = 0;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
			esp_wifi_connect();
			xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
			s_retry_num++;
			ESP_LOGI(WIFI_TAG, "retry to connect to the AP");
		}
		ESP_LOGI(WIFI_TAG,"connect to the AP fail");
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(WIFI_TAG, "got ip:%s",
						 ip4addr_ntoa(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
	}
}

void wifi_init_sta()
{
    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
											WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler,
											NULL
											));
    ESP_ERROR_CHECK(esp_event_handler_register(
											IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler,
											NULL
											));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_ESP_WIFI_SSID,
            .password = CONFIG_ESP_WIFI_PASSWORD
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(WIFI_TAG, "wifi_init_sta finished.");
    ESP_LOGI(WIFI_TAG, "connect to ap SSID:%s password:%s",
             CONFIG_ESP_WIFI_SSID, CONFIG_ESP_WIFI_PASSWORD);
}



void app_main()
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
		   chip_info.cores,
		   (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
		   (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    printf("silicon revision %d, ", chip_info.revision);

    printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
		   (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");


	/* Init WiFi as a station */
	wifi_init_sta ();

	/* Wait for the WiFi connection */
	while ((xEventGroupWaitBits (s_wifi_event_group,
								 WIFI_CONNECTED_BIT,
								 pdFALSE,
								 pdFALSE,
								 10 / portTICK_PERIOD_MS) \
			& WIFI_CONNECTED_BIT) == 0) {
		/* printf("waiting for connection...\n"); */
		/* fflush (stdout); */
	}

	/* Hardware init stuff */
	ws2812_init_spi ();

	/* SECTION: create the button queue */
	button_queue = xQueueCreate ( 5, sizeof (enum BUTTON_EVENT));

#if 1
	display_task_start ();

	gg_https_login ("", "");

	gg_start_websockets ();
#endif


	/* TODO:  */
	/* wait for the WSS socket open flag */

	enum { WAITING_FOR_WSS, NONE, ORDER_SENT, } cur_state = WAITING_FOR_WSS;
	enum BUTTON_EVENT be;

	TxBuff profiles = {0, 0, 0x1, 1};
	profiles.len = asprintf (
		&profiles.buff,
		"[\"get\",{\"data\":{\"lat\":" CONFIG_HOME_LAT ",\"lng\":"CONFIG_HOME_LNG"},\"url\":\"/v1/socket/profiles\"}]");
	TxBuff nearby = {NULL, 0, 0x01, 2};
	nearby.len = asprintf (
		&nearby.buff,
		"[\"get\",{\"data\":{\"lat\":" CONFIG_HOME_LAT  ",\"lng\":" CONFIG_HOME_LNG "},\"url\":\"/v1/socket/nearbyDrivers\"}]");
	TxBuff create_order = {NULL, 0, 0x01, 3};
	create_order.len = asprintf (
		&create_order.buff,
		"[\"post\",{\"data\":{\"lat\":" CONFIG_HOME_LAT ",\"lng\": "CONFIG_HOME_LNG",\"address\":\"" CONFIG_HOME_ADDR "\",\"type\":11,\"country\":\"AM\"},\"url\":\"/v1/socket/createOrder\"}]");

	while (1) {

		TxBuff rx;
		if (xQueueReceive (rx_queue, &rx, 10 / portTICK_PERIOD_MS)) {

			ESP_LOGW ("MAIN", "<-- Recv: %s", rx.buff);

			char *tmp = rx.buff;

			if (tmp[0] == '4') {
				if (tmp[1] == '3') {
					/* next comes the message id */
					int msg_id = atoi (&tmp[2]);
					char *json_s = &tmp[strspn (tmp, "01234567879")];

					if (msg_id == create_order.msg_id) {
						create_order_handler (msg_id, json_s);
					}
				}
				if (tmp[1] == '2') {
					/* 42 is the status */
					/* e.g. ["status",{"orders":[],"notifications":[]} */
					/* TODO: add parsing */
				}
			}
			else if (tmp[0] == '0') {
				/* TODO: parsing */
			}


			free (rx.buff);
		}


		switch (cur_state) {
		case WAITING_FOR_WSS:
			if ((xEventGroupWaitBits (wss_event_group,
									  WSS_CONNECTED,
									  pdFALSE,
									  pdFALSE,
									  10 / portTICK_PERIOD_MS)) == WSS_CONNECTED) {
				xQueueReset (button_queue);



				cur_state = NONE;

				/* send request for the profiles drivers */
				xQueueSend (tx_queue, &profiles, (TickType_t) 0);
			}
			break;
		case NONE:
			if (xQueueReceive( button_queue, &( be ),
							   ( TickType_t ) 10 / portTICK_PERIOD_MS )) {
				/* button pressed */
				ESP_LOGI ("MAIN", "Button %d was pressed!", be);
				if (be == BUT_EV_1) {
					/* TODO: send the order */

					xQueueSend (tx_queue, &nearby, (TickType_t) 0);
				}
				else if (be == BUT_EV_2) {

					xQueueSend (tx_queue, &create_order, (TickType_t) 0);
				}
			}
			break;
		default:
			break;
		}
	}

    for (; ; ) {
        printf("...\n");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }



    /* esp_restart(); */
}
