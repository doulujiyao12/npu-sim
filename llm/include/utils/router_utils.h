#pragma once
#include "defs/enums.h"

Directions get_oppose_direction(Directions dir);
int get_input_source(Directions dir, int pos);

bool is_margin_core(int id);

int decide_next_hop(int id);

Directions get_next_hop(int des, int pos);
Directions get_next_hop_r(int des, int pos);