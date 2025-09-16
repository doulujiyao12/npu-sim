#pragma once
#include "systemc.h"

#include "common/memory.h"
#include "prims/prim_base.h"

class CompBase : public PrimBase {
public:
    int inp_offset;  // 必填，在json文件中定义
    int data_offset; // 可以推算，在parseJson()中定义计算方法
    int out_offset;  // 可以推算，在parseJson()中定义计算方法

    int input_size; // 必填，在initialize()中定义计算方法
    int out_size;  // 必填，在initialize()中定义计算方法

    // 参数信息，必填，在构造函数中初始化
    vector<string> param_name;

    // 数据块信息，必填，在initialize()中定义计算方法
    vector<int> data_size_input;
    vector<pair<string, int>> data_chunk;

    // 在initializeDefault()中初始化
    int data_byte;

    // 用于dram和sram的数据搬运，以及原语之间的数据传递
    SramPosLocator *sram_pos_locator;
    AddrDatapassLabel datapass_label;

    virtual void parseJson(json j) = 0;
    virtual void initialize() = 0;
    void initializeDefault();

    void parseAddress(json j);
    void parseSramLabel(json j);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);
    void parseJson(json j);

    void checkInputData(TaskCoreContext &context, uint64_t &dram_time,
                        uint64_t inp_global_addr, vector<int> data_size_input);
    void checkStaticData(TaskCoreContext &context, uint64_t &dram_time,
                         uint64_t label_global_addr, int data_size_label,
                         string label_name, bool use_pf = false);
    void prefReadData(TaskCoreContext &context, uint64_t &dram_time,
                      int data_size_label, string label_name);
    void writeOutputData(TaskCoreContext &context, uint64_t exu_flops,
                         uint64_t sfu_flops, uint64_t dram_time,
                         uint64_t &overlap_time, int data_size_out,
                         uint64_t out_global_addr);

    CompBase() { prim_type = COMP_PRIM; }

private:
    // 在获得param_name后从json中读取或是在deserialize()中初始化
    unordered_map<string, int> param_value;
    // 在initializeDefault()中初始化
    unordered_map<string, int> data_chunk_addr;
};