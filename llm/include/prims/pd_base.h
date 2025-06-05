#pragma once

#include "common/memory.h"
#include "common/pd.h"
#include "prims/comp_base.h"

#include <vector>
using namespace std;

class pd_base : public comp_base {
public:
    double eof_chance;

    vector<Stage> batchInfo;
    vector<bool> *decode_done;
    
    void parse_address(json j);
    void parse_sram_label(json j);

    pd_base() { prim_type = PD_PRIM; }
};