#ifndef DISPLAY_H
#define DISPLAY_H

enum DisplayState {
	IDLE		= 0,
	SEARCHING	= 1,
	EN_ROUTE    = 2,
	IN_PLACE    = 3,
	IN_PROGRESS = 4,
	ENDED       = 5,
	CANCELED    = 6,
	NONE = -1,
};

extern enum DisplayState display_state;

void display_task_start ();

void display_state_set (enum DisplayState);
void display_state_inc ();

void display_set_distance (double dist);

#endif
