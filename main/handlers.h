#ifndef HANDLERS_H
#define HANDLERS_H

#include "common.h"

int create_order_handler (int msg_id, char *json_s);
int get_profiles_handler (int msg_id, char *json_s);

int get_tariffs_handler (int msg_id, char *json_s, Tariff_t **types, int *size);

void status_update_handler (char *json_s);

#endif
