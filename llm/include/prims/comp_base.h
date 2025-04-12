#pragma once
#include "systemc.h"

#include "common/memory.h"
#include "prims/prim_base.h"

class comp_base : public prim_base {
public:
    int inp_offset;
    int data_offset;
    int out_offset;

    // 以下三者，在相关参数确定之后，是可以被计算出来的
    int inp_size;
    int p_inp_size;
    int out_size;

    vector<int> out_dim;

    // 用于dram和sram的数据搬运，以及原语之间的数据传递
    SramPosLocator *sram_pos_locator;
    SramDatapassLabel datapass_label;

    virtual void parse_json(json j) = 0;

    void parse_address(json j);
    void parse_sram_label(json j);
    // HardwareTaskConfig generate_hw_config() { return HardwareTaskConfig(); }
};