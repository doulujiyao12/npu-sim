#pragma once

#include "common/memory.h"
#include "common/pd.h"
#include "prims/comp_base.h"

#include <vector>
using namespace std;

class pd_base : public CompBase {
public:
    double eof_chance;

    vector<Stage> batchInfo;
    vector<bool> *decode_done;

    void parseAddress(json j);
    void parseSramLabel(json j);

    pd_base() { prim_type = PD_PRIM; }
};