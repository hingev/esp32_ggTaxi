#ifndef COMMON_H
#define COMMON_H

extern QueueHandle_t button_queue;
enum BUTTON_EVENT {
	BUT_EV_1, BUT_EV_2
};

extern EventGroupHandle_t wss_event_group;
#define WSS_CONNECTED			BIT1

extern QueueHandle_t tx_queue;
struct TX_BUFF {
	char *buff;
	int len;
	/* encap settings .. */
	int opcode;
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

	uint16_t len_ex;

	/* uint64_t len_ex_ex; */
};


#endif
