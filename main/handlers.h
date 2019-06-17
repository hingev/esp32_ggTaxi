#ifndef HANDLERS_H
#define HANDLERS_H

int create_order_handler (int msg_id, char *json_s);
int get_profiles_handler (int msg_id, char *json_s);

void status_update_handler (char *json_s);

#endif
