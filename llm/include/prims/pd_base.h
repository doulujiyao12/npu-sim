#pragma once

#include "common/memory.h"
#include "common/pd.h"
#include "prims/prim_base.h"

#include <vector>
using namespace std;

class pd_base : public prim_base {
public:
    int inp_offset;
    int data_offset;
    int out_offset;

    // 以下三者，在相关参数确定之后，是可以被计算出来的
    int inp_size;
    int p_inp_size;
    int out_size;

    double eof_chance;

    SramPosLocator *sram_pos_locator;
    AddrDatapassLabel datapass_label;
    vector<Stage> batchInfo;
    vector<bool> *decode_done;

    virtual void parse_json(json j) = 0;

    void parse_address(json j);
    void parse_sram_label(json j);
};