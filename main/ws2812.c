#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "ws2812.h"

static spi_device_handle_t spi;

static void ws2812_add_byte (DataQueue *dq, uint8_t b) {
	size_t i;
	uint8_t cur_nibble = 0;
	for (i=0; i < 8; ++i) {
		if (b & (1 << i))
			cur_nibble = 0b1100;
		else
			cur_nibble = 0b1000;

		if (i % 2 == 0) {
			dq->data[dq->idx] = (cur_nibble << 4);
		} else {
			dq->data[dq->idx] |= cur_nibble;
			dq->idx ++;
		}
	}
}

static void ws2812_add_color (DataQueue *dq, Color c) {
	ws2812_add_byte (dq, c.g);
	ws2812_add_byte (dq, c.r);
	ws2812_add_byte (dq, c.b);
}

void ws2812_send_colors (Color *c, uint8_t count) {
	esp_err_t ret;
	size_t color_len = (24 * 4 / 8 * count) + 1;
	char * data = malloc (color_len);

	DataQueue dq = {0, (uint8_t *)data};

	size_t i;
	for (i=0; i < count; ++i) {
		ws2812_add_color (&dq, c[i]);
		//data[i] = 0b11001100;
	}
	data[color_len - 1] = 0xf0;

	spi_transaction_t t;
	memset(&t, 0, sizeof(t));       //Zero out the transaction
	t.length=color_len*8;                 //Len is in bytes, transaction length is in bits.
	t.tx_buffer=data;               //Data
	t.user=(void*)1;                //D/C needs to be set to 1
	/* ret=spi_device_polling_transmit(spi, &t);  //Transmit! */
	ret=spi_device_queue_trans(spi, &t, portMAX_DELAY);

	/* ESP_LOGI ("WS2812", "return code: %d", ret); */

	assert(ret==ESP_OK);            //Should have had no issues.

}

void spi_pre_transfer_callback(spi_transaction_t *t)
{
    /* int dc=(int)t->user; */
    /* gpio_set_level(PIN_NUM_DC, dc); */
}


void ws2812_init_spi (void) {
	esp_err_t ret;


	spi_bus_config_t buscfg={
		.miso_io_num=12,
		.mosi_io_num=13,
		.sclk_io_num=14,
		.quadwp_io_num=-1,
		.quadhd_io_num=-1,
		.max_transfer_sz=24*4/8*20			/* FIXME: ..? */
	};
	spi_device_interface_config_t devcfg={
		.clock_speed_hz=3333333,           //Clock out at 300 nS
		.mode=0,                                //SPI mode 0
		.spics_io_num=15,               //CS pin
		.queue_size=7,                          //We want to be able to queue 7 transactions at a time
		.pre_cb=spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
	};
	//Initialize the SPI bus
	ret=spi_bus_initialize(HSPI_HOST, &buscfg, 1);
	ESP_ERROR_CHECK(ret);

	//Attach the LCD to the SPI bus
	ret=spi_bus_add_device(HSPI_HOST, &devcfg, &spi);
	ESP_ERROR_CHECK(ret);

	/* each real bit is 4 bits */
	/* either 1000 or 1100 */
	ESP_LOGD ("WS2812", "SPI hardware init done!");



}
