#pragma once
#include "common/system.h"
#include "defs/const.h"
#include "defs/enums.h"
#include "defs/global.h"
#include "systemc.h"

#include "nlohmann/json.hpp"
#include <vector>

using json = nlohmann::json;

class PrimBase {
public:
    PrimCoreContext *prim_context;

    int prim_type;
    string name;

    int sram_addr;
    DATATYPE datatype = INT8;

    virtual int taskCoreDefault(TaskCoreContext &context) = 0;
    virtual sc_bv<128> serialize() = 0;
    virtual void deserialize(sc_bv<128> buffer) = 0;
    virtual void printSelf() = 0;

    PrimBase() {
        name = "PrimBase";
        prim_type |= NORM_PRIM;
    }
    virtual ~PrimBase() = default;
};


class CompBase : public PrimBase {
public:
    // 地址偏移信息
    int inp_offset;  // 必填，在json文件中
    int data_offset; // 选填，可以推算，在parseJson()中
    int out_offset;  // 选填，可以推算，在parseJson()中

    // 数据块信息
    vector<int> data_size_input;          // 必填，在initialize()中
    vector<pair<string, int>> data_chunk; // 必填，在initialize()中
    int input_size;                       // 可以推算，在initializeDefault()中
    int out_size;                         // 可以推算，initializeDefault()中
    unordered_map<string, int>
        data_chunk_addr; // 可以推算，在initializeDefault()中

    // 参数信息
    vector<string> param_name; // 必填，在构造函数中
    unordered_map<string, int>
        param_value; // 可以推算，在json中或在deserialize()中

    // 在initializeDefault()中
    int data_byte;

    // 实行多态的函数，每当有新原语被注册，需要实现这两个函数
    virtual void initialize() = 0;
    virtual int taskCore(TaskCoreContext &context, string prim_name,
                         u_int64_t dram_time, u_int64_t &exu_ops,
                         u_int64_t &sfu_ops) = 0;

    // 暴露出的工作函数
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);
    void parseJson(json j);

    int sramUtilization(DATATYPE datatype, int cid = 0);
    void printSelf();

    // 内存存取
    void checkStaticData(TaskCoreContext &context, uint64_t &dram_time,
                         uint64_t label_global_addr, int data_size_label,
                         string label_name, bool use_pf = false);
    void prefReadData(TaskCoreContext &context, uint64_t &dram_time,
                      int data_size_label, string label_name);

    CompBase() { prim_type |= COMP_PRIM; }

private: // 对不同原语隐藏实现
    // 初始化工具函数
    void initializeDefault();
    void parseAddress(json j);   // dram 地址
    void parseSramLabel(json j); // sram 标签

    // taskCore中的内存操作
    void checkInputData(TaskCoreContext &context, uint64_t &dram_time,
                        uint64_t inp_global_addr, vector<int> data_size_input);
    void writeOutputData(TaskCoreContext &context, uint64_t exu_flops,
                         uint64_t sfu_flops, uint64_t dram_time,
                         uint64_t &overlap_time, int data_size_out,
                         uint64_t out_global_addr);
};


class GpuBase : public CompBase {
public:
    int grid_x;
    int grid_y;
    int block_x;
    int block_y;

    int inp_size;
    int out_size;

    bool mock;
    int req_sm;
    int fetch_index; // 用于记录取权重需要偏移的offset

    void parse_compose(json j);
    void parse_addr_label(json j);

    virtual void parseJson(json j) = 0;
    virtual GpuBase *clone() = 0;
    void initialize() {}

    GpuBase() { prim_type |= GPU_PRIM; }
};


class PdBase : public CompBase {
public:
    PdBase() {
        prim_type |= PD_PRIM;
        param_name.insert(param_name.end(), {"job_type"});
    }
};


class MoeBase : public CompBase {
public:
    MoeBase() {
        prim_type |= MOE_PRIM;
        param_name.insert(param_name.end(), {"need_choose"});
    }
};