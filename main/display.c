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

static EventGroupHandle_t display_event_group;

static void display_task (void *pvParameters) {

	Color *leds = heap_caps_malloc (LED_CNT * sizeof (Color),MALLOC_CAP_DMA );
	int r1;

	while (1) {
		r1 = esp_random () % LED_CNT;
		switch (display_state) {
		case IDLE:
			memset (leds, 0, LED_CNT * sizeof (Color));
			leds[r1].r = 255;
			leds[r1].g = 255;
			leds[r1].b = 255;
			ws2812_send_colors (leds, LED_CNT);
			break;
		default:
			break;
		}

		printf ("###\n");
		vTaskDelay (300 / portTICK_PERIOD_MS);
	}

	free (leds);
}

void display_task_start () {
    display_event_group = xEventGroupCreate();

		xTaskCreate(&display_task, "display_task", 2048, NULL, 5, NULL);
}

void display_state_set (enum DisplayState st) {

}
