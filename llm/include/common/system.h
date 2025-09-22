#pragma once
#include "defs/const.h"
#include "defs/enums.h"
#include "trace/Event_engine.h"
#include "unit_module/sram_manager/sram_manager.h"
#include "common/memory.h"
#include "common/include.h"

#include <vector>

using namespace std;

class SramManager;
class SramPosLocator;
class GpuPosLocator;

// prim与计算核共用
class PrimCoreContext {
public:
    int cid;
    int loop_cnt; // 当前prim执行的循环次数

    SramManager *sram_manager_;
    SramPosLocator *sram_pos_locator_;
    GpuPosLocator *gpu_pos_locator_;

    // serving相关
    vector<Stage> batch_info_;
    vector<bool> decode_done_;

    // 原语的输入输出标签
    AddrDatapassLabel *datapass_label_;

    // moe相关
    vector<int> selected_experts_;   // 选中的专家列表
    vector<int> selected_freq_;      // 专家被选中的次数
    vector<int> prefetched_experts_; // 被预先存储在sram中的专家

    PrimCoreContext() {
        datapass_label_ = new AddrDatapassLabel();
        sram_manager_ = nullptr;
        sram_pos_locator_ = nullptr;
        gpu_pos_locator_ = nullptr;
    }

    PrimCoreContext(int id) : cid(id) {
        loop_cnt = 0;
        
        sram_manager_ =
            new SramManager(0, cid, MAX_SRAM_SIZE, SRAM_BLOCK_SIZE, 0);
        sram_pos_locator_ = new SramPosLocator(cid, sram_manager_);
        datapass_label_ = new AddrDatapassLabel();
    }

    ~PrimCoreContext() {
        if (sram_manager_) delete sram_manager_;
        if (sram_pos_locator_) delete sram_pos_locator_;
        if (datapass_label_) delete datapass_label_;
    }
};