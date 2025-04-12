#pragma once
#include <queue>

#include "common/msg.h"
#include "monitor/config_helper_base.h"

using namespace std;

class Config_helper_core : public config_helper_base {
public:
    Config_helper_core(string filename, string font_ttf);

    void generate_prims(int i);
    void calculate_address(bool do_loop);

    void print_self();

    void random_core(string font_ttf);

    void fill_queue_start(queue<Msg> *q);
    void fill_queue_config(queue<Msg> *q);
    CoreConfig *get_core(int id);
};