#pragma once

#include "common/memory.h"
#include "prims/comp_base.h"

#include <vector>
using namespace std;

class moe_base : public CompBase {
public:
    bool need_choose;              // 是否需要重选专家
    vector<int> *selected_experts; // 选中的专家列表
    vector<int> *selected_freq; // 专家被选中的次数
    vector<int> *prefetched_experts; // 被预先存储在sram中的专家

    moe_base() { prim_type = MOE_PRIM; }
};