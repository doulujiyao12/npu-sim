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

    int data_byte;

    // 用于dram和sram的数据搬运，以及原语之间的数据传递
    SramPosLocator *sram_pos_locator;
    AddrDatapassLabel datapass_label;

    virtual void parse_json(json j) = 0;

    void parse_address(json j);
    void parse_sram_label(json j);
    void initialize() {}

    void check_input_data(TaskCoreContext &context, uint64_t &dram_time,
                          uint64_t inp_global_addr, vector<int> data_size_input);
    void check_static_data(TaskCoreContext &context, uint64_t &dram_time,
                           uint64_t label_global_addr, int data_size_label,
                           string label_name, bool use_pf = false);
    void perf_read_data(TaskCoreContext &context, uint64_t &dram_time,
                           int data_size_label,
                           string label_name);
    void write_output_data(TaskCoreContext &context, uint64_t exu_flops,
                           uint64_t sfu_flops, uint64_t dram_time,
                           uint64_t &overlap_time, int data_size_out,
                           uint64_t out_global_addr);

    comp_base() { prim_type = COMP_PRIM; }
};