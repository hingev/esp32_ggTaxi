
#ifndef WS2812_H
#define WS2812_H

struct __Color__ {
		uint8_t r;
		uint8_t g;
		uint8_t b;
};

typedef struct __Color__ Color;

typedef struct {
	size_t idx;
	uint8_t *data;
	size_t max_size;
} DataQueue;

void ws2812_init_spi (void);
void ws2812_send_colors (Color *c, uint8_t count);

#endif
