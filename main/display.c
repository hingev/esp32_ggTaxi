#include <string.h>
#include <stdio.h>
#include <math.h>
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


static double display_en_route_distance = 0;
static float display_tariff_dram = 0;

void display_set_distance (double dist) {
	display_en_route_distance = dist;
}

void display_set_tariff (float dram) {
	display_tariff_dram = dram;
}

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

	int r1 = 0;
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
								   0xFFFF,
								   1, /* xClearOnExit */
								   0, /* xWaitForAllBits */
								   30 / portTICK_PERIOD_MS);
		if (res) {
			if (res & (1UL << IDLE)) {
				display_state = IDLE;
			}
			else if (res & (1UL << SEARCHING)) {
				display_state = SEARCHING;
			}
			else if (res & (1UL << EN_ROUTE)) {
				display_state = EN_ROUTE;
			}
			else if (res & (1UL << IN_PLACE)) {
				display_state = IN_PLACE;
			}
			else if (res & (1UL << IN_PROGRESS)) {
				display_state = IN_PROGRESS;
			}
			else if (res & (1UL << ENDED)) {
				display_state = IDLE_READY;
			}
			else if (res & (1UL << CANCELED)) {
				display_state = IDLE_READY;
			}
			else if (res & (1UL << TARIFF)) {
				display_state = TARIFF;
			}
			else if (res & (1UL << IDLE_READY)) {
				display_state = IDLE_READY;
			}
			/* reset the state machine */
			step = 0;
			cur_led = 0;
			sub_state = 0;
		}

		int tmp = r1;
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
		case IDLE_READY:
			if (sub_state == 0) {
				/* printf ( "picking led"); */
				do {
					tmp = esp_random () % LED_CNT;
				} while (tmp == r1);
				r1 = tmp;
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
				if (display_state == IDLE_READY)
					leds[cur_led].g = step;
				else
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
				if (display_state == IDLE_READY)
					leds[cur_led].g = step;
				else
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
		case EN_ROUTE:
			tmp = 0 ;
			/* FIXME: get a good logarithmic scale here */
			if (display_en_route_distance <= 100) {
				tmp = 1;
			}
			else if (display_en_route_distance > 100 &&
							 display_en_route_distance <= 300) {
				tmp = 2;
			}
			else if (display_en_route_distance > 300 &&
							 display_en_route_distance <= 600) {
				tmp = 3;
			}
			else if (display_en_route_distance > 600) {
				tmp = display_en_route_distance / 100;
				if (tmp > LED_CNT)
					tmp = LED_CNT;
			}

			/* pulsate tmp leds */
			if (sub_state == 0) {
				memset (leds, 0, LED_CNT * sizeof (Color));
				sub_state = 1;
			}
			else if (sub_state == 1) {
				step += 20;
				for (i=0; i < tmp; ++i) {
					leds[i].r = step;
					leds[i].b = step;

				}
				if (step > 120)
					sub_state = 2;
			}
			else if (sub_state == 2) {
				for (i=0; i < LED_CNT; ++i) {
					leds[i].r *= .8;
					leds[i].b *= .8;
				}

				if (leds[0].r == 0) {
					sub_state = 1;
					step = 0;
				}
			}
			ws2812_send_colors (leds, LED_CNT);
			break;
		case IN_PLACE:
			if (sub_state == 0) {
				memset (leds, 0, LED_CNT * sizeof (Color));
				sub_state = 1;
			}
			else if (sub_state == 1) {
				leds[(cur_led) % LED_CNT].g = step;
				leds[(cur_led+4) % LED_CNT].g = step;
				leds[(cur_led+8) % LED_CNT].g = step;
				leds[(cur_led+12) % LED_CNT].g = step;
				leds[(cur_led) % LED_CNT].r = step;
				leds[(cur_led+4) % LED_CNT].r = step;
				leds[(cur_led+8) % LED_CNT].r = step;
				leds[(cur_led+12) % LED_CNT].r = step;

				step += 75;
				if (step > 200) {
					sub_state = 2;
					step = 0;
				}
			}
			else if (sub_state == 2) {
				leds[(cur_led) % LED_CNT].g *= 0.8;
				leds[(cur_led+4) % LED_CNT].g *= 0.8;
				leds[(cur_led+8) % LED_CNT].g *= 0.8;
				leds[(cur_led+12) % LED_CNT].g *= 0.8;
				leds[(cur_led) % LED_CNT].r *= 0.8;
				leds[(cur_led+4) % LED_CNT].r *= 0.8;
				leds[(cur_led+8) % LED_CNT].r *= 0.8;
				leds[(cur_led+12) % LED_CNT].r *= 0.8;

				if (leds[cur_led % LED_CNT].g == 0) {
					sub_state = 1;
				}
			}
			ws2812_send_colors (leds, LED_CNT);
			break;
		case IN_PROGRESS:
			if (sub_state == 0) {
				memset (leds, 0, LED_CNT * sizeof (Color));
				sub_state = 1;
			}
			else if (sub_state == 1) {
				leds[(cur_led) % LED_CNT].g = step;
				leds[(cur_led+4) % LED_CNT].g = step;
				leds[(cur_led+8) % LED_CNT].g = step;
				leds[(cur_led+12) % LED_CNT].g = step;

				step += 75;
				if (step > 200) {
					sub_state = 2;
					step = 0;
				}
			}
			else if (sub_state == 2) {
				leds[(cur_led) % LED_CNT].g *= 0.8;
				leds[(cur_led+4) % LED_CNT].g *= 0.8;
				leds[(cur_led+8) % LED_CNT].g *= 0.8;
				leds[(cur_led+12) % LED_CNT].g *= 0.8;

				if (leds[cur_led % LED_CNT].g == 0) {
					sub_state = 0;
					cur_led ++;
					if (cur_led == LED_CNT)
						cur_led = 0;
				}
			}
			ws2812_send_colors (leds, LED_CNT);
			break;
		case TARIFF:
			if (sub_state == 0) {
				memset (leds, 0, LED_CNT * sizeof (Color));
				for (i=0; i < LED_CNT && i < display_tariff_dram / 100.; ++i) {
					leds[i].g = 20;
				}
				ws2812_send_colors (leds, LED_CNT);
				sub_state = 1;
			}
			else {
				sub_state ++;
			}
			if (sub_state > 70) {
				display_state_set (IDLE_READY);
			}
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
	ESP_LOGW ("DISP", "Setting display state to %lx", (1UL << st));
	xEventGroupSetBits (display_event_group,
						(uint32_t)(1UL << st));

}
