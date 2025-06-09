#pragma once
#include "defs/const.h"
#include "defs/enums.h"
#include "memory/dram/Dcachecore.h"
#include "memory/dram/GPUNB_DcacheIF.h"
#include "memory/dram/NB_DcacheIF.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/High_mem_access_unit.h"
#include "memory/sram/Mem_access_unit.h"
#include "trace/Event_engine.h"
#include "link/nb_global_memif_v2.h"
#include "unit_module/sram_manager/sram_manager.h"

#include <vector>

using namespace std;


class HardwareTaskConfig {
public:
    HardwareConduct hardware;
    vector<float *> data;
    vector<int> args;
};

class GPT2Config {
public:
    int max_seq_len;       // max sequence length, e.g. 1024
    int vocab_size;        // vocab size, e.g. 50257
    int padded_vocab_size; // padded to e.g. %128==0, 50304
    int num_layers;        // number of layers, e.g. 12
    int num_heads;         // number of heads in attention, e.g. 12
    int channels;          // number of channels, e.g. 768
};

class TaskCoreContext {
public:
    mem_access_unit *mau;
    high_bw_mem_access_unit *hmau;
    mem_access_unit *temp_mau;
    high_bw_mem_access_unit *temp_hmau;
    sc_bv<SRAM_BITWIDTH> msg_data;
    int *sram_addr;
    SramManager* sram_manager_;
    AllocationID alloc_id_;
    sc_event *s_nbdram;
    sc_event *e_nbdram;
    int loop_cnt;
    uint64_t MaxDramAddr; // 当前核最大的 dram 地址
    
    NB_GlobalMemIF *nb_global_memif;
    sc_event *start_global_event;
    sc_event *end_global_event;

    void SetGlobalMemIF(NB_GlobalMemIF *nb_global_memif, sc_event *start_global_event, sc_event *end_global_event) {
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
    int *cid;
    Event_engine *event_engine;
    sc_event *start_nb_gpu_dram_event;
    sc_event *end_nb_gpu_dram_event;
#endif
#if USE_NB_DRAMSYS == 1
    // 构造函数
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau, 
                    mem_access_unit *temp_mau, high_bw_mem_access_unit *temp_hmau,
                    const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, SramManager* sram_manager, uint64_t MaxDramAddr)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          msg_data(msg_data),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          sram_manager_(sram_manager),
          MaxDramAddr(MaxDramAddr) {}
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau, high_bw_mem_access_unit *temp_hmau,
                    const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, SramManager* sram_manager, int loop_cnt, uint64_t MaxDramAddr)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          msg_data(msg_data),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          loop_cnt(loop_cnt),
          sram_manager_(sram_manager),
          MaxDramAddr(MaxDramAddr) {}
#else
    // 构造函数
    TaskCoreContext(DcacheCore *wc, mem_access_unit *mau,
                    high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau, high_bw_mem_access_unit *temp_hmau,
                    const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram, SramManager* sram_manager, uint64_t MaxDramAddr)
        : wc(wc),
          mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          msg_data(msg_data),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          sram_manager_(sram_manager),
          MaxDramAddr(MaxDramAddr) {}
#endif

#if USE_L1L2_CACHE == 1
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau,
                    mem_access_unit *temp_mau, high_bw_mem_access_unit *temp_hmau,
                    const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr,
                    sc_event *s_nbdram, sc_event *e_nbdram,
                    NB_DcacheIF *nb_dcache, int loop_cnt, SramManager* sram_manager,
                    sc_event *start_nb_gpu_dram_event,
                    sc_event *end_nb_gpu_dram_event,
                    uint64_t MaxDramAddr)
        : mau(mau),
          hmau(hmau),
          temp_mau(temp_mau),
          temp_hmau(temp_hmau),
          msg_data(msg_data),
          sram_addr(sram_addr),
          s_nbdram(s_nbdram),
          e_nbdram(e_nbdram),
          nb_dcache(nb_dcache),
          loop_cnt(loop_cnt),
          sram_manager_(sram_manager),
          start_nb_gpu_dram_event(start_nb_gpu_dram_event),
          end_nb_gpu_dram_event(end_nb_gpu_dram_event),
          MaxDramAddr(MaxDramAddr) {}
#endif
};

class CoreHWConfig {
public:
    int MAC_size;
    int sram_bitwidth;
    int sram_banks;

    CoreHWConfig(int mac_size, int sram_bitwidth, int sram_banks)
        : MAC_size(mac_size), sram_bitwidth(sram_bitwidth), sram_banks(sram_banks) {}
    CoreHWConfig() : MAC_size(128), sram_bitwidth(128), sram_banks(0) {}
};