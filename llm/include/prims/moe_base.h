#pragma once

#include "common/memory.h"
#include "prims/comp_base.h"

#include <vector>
using namespace std;

class moe_base : public comp_base {
public:
    bool need_choose;              // 是否需要重选专家
    vector<int> *selected_experts; // 选中的专家列表

    moe_base() { prim_type = MOE_PRIM; }
};