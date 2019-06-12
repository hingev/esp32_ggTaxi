#ifndef DISPLAY_H
#define DISPLAY_H

enum DisplayState {
	IDLE = 0,
	SEARCHING = 1,
	FOUND=2,
};

enum DisplayState display_state;

void display_task_start ();

void display_state_set (enum DisplayState);
void display_state_inc ();

#endif
