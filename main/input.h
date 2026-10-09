// The three ladder keys on GPIO0, sampled by a 100 Hz task.
//
// It is one resistor ladder, so only one key reads at a time: holding UP hides
// a press of OK. Controls that need two keys use one after the other instead.
#pragma once

#include <stdbool.h>

void input_init(void);
// One frame's view: held state plus edges, cleared when read.
typedef struct {
    bool up, down, ok;          // held right now
    bool up_pressed, down_pressed, ok_pressed;
    bool up_released, down_released, ok_released;
    bool any_pressed;
} input_state_t;
input_state_t input_take(void);
// Seconds since a key last went down or up.
float input_idle_s(void);
