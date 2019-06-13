#include <string.h>
#include <stdio.h>
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

#include "ws2812.h"
#include "display.h"

#define LED_CNT								16

static TaskHandle_t xHandle = NULL;
static EventGroupHandle_t display_event_group;

static void display_task (void *pvParameters) {

	Color *leds = malloc (LED_CNT * sizeof (Color) );

	assert (leds != NULL);

	int r1;
	int sub_state = 0;
	int cur_led = 0;
	int step = 0;
	int i;

	while (1) {
		switch (display_state) {
		case IDLE:
			if (sub_state == 0) {
				printf ( "picking led");
				r1 = esp_random () % LED_CNT;
				memset (leds, 0, LED_CNT * sizeof (Color));
				cur_led = r1;
				ws2812_send_colors (leds, LED_CNT);
				sub_state = 1;
				step = 0;
			}
			else if (sub_state == 1) {
				printf ( "Increaing /");
				step ++;
				leds[cur_led].r = step;
				leds[cur_led].g = 0;
				leds[cur_led].b = 0;

				if (step == 30)
					sub_state = 2;
				ws2812_send_colors (leds, LED_CNT);
			}
			else if (sub_state == 2) {
				printf ( "decreaing /");
				step -- ;
				leds[cur_led].r = step;
				leds[cur_led].g = 0;
				leds[cur_led].b = 0;

				if (step == 0)
					sub_state = 0;
				ws2812_send_colors (leds, LED_CNT);
			}
			break;
		case SEARCHING:

			break;
		default:
			break;
		}

		printf ( "###");
		/* printf ("###\n"); */
		vTaskDelay (30 / portTICK_PERIOD_MS);
	}

	free (leds);

	vTaskDelete ( xHandle );
}

void display_task_start () {
    display_event_group = xEventGroupCreate();

		xTaskCreate(&display_task, "display_task", 2048, NULL, 5, &xHandle);
}

void display_state_set (enum DisplayState st) {

}
