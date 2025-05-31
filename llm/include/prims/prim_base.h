#pragma once
#include "common/system.h"
#include "defs/const.h"
#include "defs/enums.h"
#include "defs/global.h"
#include "systemc.h"

#include "nlohmann/json.hpp"
#include <vector>

using json = nlohmann::json;

class prim_base {
public:
    int cid;
    PRIM_TYPE prim_type;
    string name;

    bool use_hw;
    int sram_addr;
    int dram_inp_size;
    int dram_out_size;
    int dram_data_size;
    DATATYPE datatype = INT8;

    virtual int task() = 0;
    // task_core hardware accuracy simulation
    virtual int task_core(TaskCoreContext &context) = 0;
    virtual sc_bv<128> serialize() = 0;
    virtual void deserialize(sc_bv<128> buffer) = 0;
    virtual void initialize() = 0;
    virtual void print_self(string prefix) = 0;
    virtual int sram_utilization(DATATYPE datatype) = 0;

    prim_base() {
        name = "prim_base";
        prim_type = NORM_PRIM;
    }
    virtual ~prim_base() = default;
};
