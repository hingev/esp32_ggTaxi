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
#include "driver/gpio.h"

#include "common.h"
#include "ws2812.h"
#include "display.h"

enum DisplayState display_state = NONE;

#define LED_CNT								16

#define BUT_1				23
#define BUT_2				22

enum BUTTON_STATES {
	NOT_PUSHED, MAYBE_PUSHED, PUSHED,
	HOLD, MAYBE_REL
} ;
static enum BUTTON_STATES states [2] ;

static void add_state (int pin, int value) {

	value = ! (value);

	switch (states [pin]) {
	case NOT_PUSHED:
		if (value)
			states[pin] = MAYBE_PUSHED;
		break;
	case MAYBE_PUSHED:
		if (value)
			states[pin] = PUSHED;
		else
			states[pin] = NOT_PUSHED;
		break;
	case PUSHED:
		if (value)
			states[pin] = HOLD;
		else
			states[pin] = MAYBE_PUSHED;
		break;
	case HOLD:
		if (! value)
			states[pin] = MAYBE_REL;
		break;
	case MAYBE_REL:
		if (! value)
			states[pin] = NOT_PUSHED;
		break;
	default :
		break;
	}
}

static int get_state (int pin) {
	return states[pin];
}

static TaskHandle_t xHandle = NULL;
static EventGroupHandle_t display_event_group;

static void display_task (void *pvParameters) {

	/* initialize the button inputs */
	gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BUT_1) | (1ULL << BUT_2);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

	Color *leds = malloc (LED_CNT * sizeof (Color) );

	assert (leds != NULL);

	int r1;
	int sub_state = 0;
	int cur_led = 0;
	int step = 0;
	int i;

	uint32_t res;

	while (1) {
		/* check the event group bits */

		int l1 = gpio_get_level (BUT_1);
		int l2 = gpio_get_level (BUT_2);
		add_state (0, l1);
		add_state (1, l2);

		if (get_state (0) == PUSHED) {
			enum BUTTON_EVENT be = BUT_EV_1;
			xQueueSend (button_queue, &be, portMAX_DELAY);
		}
		if (get_state (1) == PUSHED) {
			enum BUTTON_EVENT be = BUT_EV_2;
			xQueueSend (button_queue, &be, portMAX_DELAY);
		}

		res = xEventGroupWaitBits (display_event_group,
								   0xFF,
								   1, /* xClearOnExit */
								   0, /* xWaitForAllBits */
								   30 / portTICK_PERIOD_MS);

		if (res) {
			if (res & (1 << IDLE)) {
				display_state = IDLE;
			}
			else if (res & (1 << SEARCHING)) {
				display_state = SEARCHING;
			}
			else if (res & (1 << EN_ROUTE)) {
				display_state = EN_ROUTE;
			}
			/* reset the state machine */
			step = 0;
			cur_led = 0;
			sub_state = 0;
		}

		switch (display_state) {
		case NONE:
			if (sub_state == 0) {
				/* Reset the state of the LEDs */
				memset (leds, 0, LED_CNT * sizeof (Color));
				ws2812_send_colors (leds, LED_CNT);
				sub_state ++;
			}
			break;
		case IDLE:
			if (sub_state == 0) {
				/* printf ( "picking led"); */
				r1 = esp_random () % LED_CNT;
				memset (leds, 0, LED_CNT * sizeof (Color));
				cur_led = r1;
				ws2812_send_colors (leds, LED_CNT);
				sub_state = 1;
				step = 0;
			}
			else if (sub_state == 1) {
				/* printf ( "Increaing /"); */
				step ++;
				leds[cur_led].r = step;
				leds[cur_led].g = 0;
				leds[cur_led].b = 0;

				if (step == 30)
					sub_state = 2;
				ws2812_send_colors (leds, LED_CNT);
			}
			else if (sub_state == 2) {
				/* printf ( "decreaing /"); */
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
			if (sub_state == 0) {
				cur_led = 0;
				memset (leds, 0, LED_CNT * sizeof (Color));
				sub_state = 1;
			}
			else if (sub_state == 1) {
				for (i=0; i < LED_CNT; ++i) {
					leds[i].b *= 0.85;
				}
				leds[cur_led].b = step;
				step += 75;
				if (step > 200) {
					cur_led ++;
					if (cur_led >= LED_CNT)
						cur_led = 0;
					step = 0;
				}
			}
			ws2812_send_colors (leds, LED_CNT);
			break;
		default:
			break;
		}

		/* printf ( "###"); */
		/* printf ("###\n"); */
		/* vTaskDelay (30 / portTICK_PERIOD_MS); */
	}

	free (leds);

	vTaskDelete ( xHandle );
}

void display_task_start () {
    display_event_group = xEventGroupCreate();

	xTaskCreate(&display_task, "display_task", 2048, NULL, 5, &xHandle);
}

void display_state_set (enum DisplayState st) {
	xEventGroupSetBits (display_event_group,
						(1 << st));

}
