#ifndef COMMON_H
#define COMMON_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

extern QueueHandle_t button_queue;
enum BUTTON_EVENT {
	BUT_EV_1, BUT_EV_2
};

extern EventGroupHandle_t wss_event_group;
#define WSS_CONNECTED			BIT1

extern QueueHandle_t tx_queue;
extern QueueHandle_t rx_queue;
struct TX_BUFF {
	char *buff;
	int len;
	/* encap settings .. */
	int opcode;
	int msg_id; 				/* used by the gg protocol */
	/* if msg_id is 0 the package is not re-packaged */
};
typedef struct TX_BUFF TxBuff;
int tx_buff_encapsulate (TxBuff **res, TxBuff *src, uint32_t mask);
struct __attribute__((__packed__)) HEADER {

	uint8_t opcode:4;
	uint8_t rsv3:1;
	uint8_t rsv2:1;
	uint8_t rsv1:1;
	uint8_t fin:1;

	uint8_t payload_len:7;
	uint8_t mask:1;

	uint16_t len_ex;			/* endianness is different */

	/* uint64_t len_ex_ex; */
};

struct STATUS {
	uint32_t order_id;
	uint32_t status_id;

	uint32_t payment_id;
	uint32_t profile_id;

	double order_lat;
	double order_lng;
};
typedef struct STATUS Status;

struct Tariff {
	int minimal;
	int type_id;
};
typedef struct Tariff Tariff_t;

extern Status cur_status;

double calc_distance (double latA, double lngA,
					  double latB, double lngB);

#endif
