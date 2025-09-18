#pragma once
#include "defs/const.h"
#include "defs/enums.h"
#include "link/nb_global_memif_v2.h"
#include "memory/dram/Dcachecore.h"
#include "memory/dram/GPUNB_DcacheIF.h"
#include "memory/dram/NB_DcacheIF.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/High_mem_access_unit.h"
#include "memory/sram/Mem_access_unit.h"
#include "memory/sram_writer.h"
#include "trace/Event_engine.h"
#include "unit_module/sram_manager/sram_manager.h"

#include <vector>

using namespace std;

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

    PrimCoreContext(int id) : cid(id) {
        loop_cnt = 0;
        
        sram_manager_ =
            new SramManager(0, cid, MAX_SRAM_SIZE, SRAM_BLOCK_SIZE, 0);
        sram_pos_locator_ = new SramPosLocator(cid, sram_manager_);
        datapass_label_ = new AddrDatapassLabel;
    }

    ~PrimCoreContext() {
        delete sram_manager_;
        delete sram_pos_locator_;
        delete datapass_label_;
    }
};

// 用于传入taskCore
class TaskCoreContext {
public:
    int cid;

    mem_access_unit *mau;
    high_bw_mem_access_unit *hmau;
    mem_access_unit *temp_mau;
    high_bw_mem_access_unit *temp_hmau;
    int *sram_addr;
    SramManager *sram_manager_;
    AllocationID alloc_id_;
    sc_event *s_nbdram;
    sc_event *e_nbdram;
    sc_event *s_sram;
    sc_event *e_sram;
    SRAMWriteModule *sram_writer;
    int loop_cnt;
    uint64_t MaxDramAddr; // 当前核最大的 dram 地址
    unsigned int defaultDataLength;

    NB_GlobalMemIF *nb_global_memif;
    sc_event *start_global_event;
    sc_event *end_global_event;

    void SetGlobalMemIF(NB_GlobalMemIF *nb_global_memif,
                        sc_event *start_global_event,
                        sc_event *end_global_event) {
        this->nb_global_memif = nb_global_memif;
        this->start_global_event = start_global_event;
        this->end_global_event = end_global_event;
    }

#if USE_NB_DRAMSYS == 1

    NB_DcacheIF *nb_dcache;


#else
    DcacheCore *wc;
#endif
#if USE_L1L2_CACHE == 1
    // tlm_utils::simple_initiator_socket<Processor> *cache_socket;
    GPUNB_dcacheIF *gpunb_dcache_if;
    Event_engine *event_engine;
    sc_event *start_nb_gpu_dram_event;
    sc_event *end_nb_gpu_dram_event;
#endif
#if USE_NB_DRAMSYS == 1
    // 构造函数
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau,
                    high_bw_mem_access_unit *temp_hmau, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, SramManager *sram_manager,
                    uint64_t MaxDramAddr, unsigned int defaultDataLength)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          sram_manager_(sram_manager),
          MaxDramAddr(MaxDramAddr),
          defaultDataLength(defaultDataLength) {}
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau,
                    high_bw_mem_access_unit *temp_hmau, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, SramManager *sram_manager,
                    int loop_cnt, uint64_t MaxDramAddr,
                    unsigned int defaultDataLength)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          loop_cnt(loop_cnt),
          sram_manager_(sram_manager),
          MaxDramAddr(MaxDramAddr),
          defaultDataLength(defaultDataLength) {}
#else
    // 构造函数
    TaskCoreContext(DcacheCore *wc, mem_access_unit *mau,
                    high_bw_mem_access_unit *hmau, mem_access_unit *temp_mau,
                    high_bw_mem_access_unit *temp_hmau, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    SramManager *sram_manager, int loop_cnt,
                    uint64_t MaxDramAddr, unsigned int defaultDataLength)
        : wc(wc),
          mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          sram_manager_(sram_manager),
          loop_cnt(loop_cnt),
          MaxDramAddr(MaxDramAddr),
          defaultDataLength(defaultDataLength) {}
#endif

#if USE_L1L2_CACHE == 1
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau,
                    high_bw_mem_access_unit *temp_hmau, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, int loop_cnt,
                    SramManager *sram_manager,
                    sc_event *start_nb_gpu_dram_event,
                    sc_event *end_nb_gpu_dram_event, uint64_t MaxDramAddr,
                    unsigned int defaultDataLength)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          loop_cnt(loop_cnt),
          sram_manager_(sram_manager),
          start_nb_gpu_dram_event(start_nb_gpu_dram_event),
          end_nb_gpu_dram_event(end_nb_gpu_dram_event),
          MaxDramAddr(MaxDramAddr),
          defaultDataLength(defaultDataLength) {}
#endif
};