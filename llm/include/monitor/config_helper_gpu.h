#pragma once
#include "monitor/config_helper_base.h"

class Config_helper_gpu : public config_helper_base {
public:
    vector<StreamConfig> streams;
    int gpu_index; // 正在发放第几个gpu start data，仅在GPU模式下使用
    GpuPosLocator *gpu_pos_locator;

    Config_helper_gpu(string filename, string font_ttf, int config_chip_id = 0);
    Config_helper_gpu* clone() const override {
        return new Config_helper_gpu(*this);
    }

    void generate_prims(int i);
    void calculate_address(bool do_loop);

    void print_self();

    void fill_queue_config(queue<Msg> *q);
    void fill_queue_start(queue<Msg> *q, int phase_cnt);
    void fill_queue_start(queue<Msg> *q) {};
};