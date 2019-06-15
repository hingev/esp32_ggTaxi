#ifndef COMMON_H
#define COMMON_H

extern QueueHandle_t button_queue;
enum BUTTON_EVENT {
	BUT_EV_1, BUT_EV_2
};

extern EventGroupHandle_t wss_event_group;
#define WSS_CONNECTED			BIT1

#endif
