#pragma once
#include "defs/const.h"
#include "defs/enums.h"
#include "memory/dram/Dcachecore.h"
#include "memory/dram/NB_dcachecore.h"
#include "memory/gpu/GPU_L1L2_Cache.h"
#include "memory/sram/High_mem_access_unit.h"
#include "memory/sram/Mem_access_unit.h"

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
    sc_bv<SRAM_BITWIDTH> msg_data;
    int *sram_addr;
    sc_event *s_nbdram;
    sc_event *e_nbdram;
    int loop_cnt;
#if USE_NB_DRAMSYS == 1
    NB_dcachecore *nb_dcache;
#else
    DcacheCore *wc;
#endif
#if USE_L1L2_CACHE == 1
    // tlm_utils::simple_initiator_socket<Processor> *cache_socket;
#endif
#if USE_NB_DRAMSYS == 1
    // 构造函数
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau, const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr, sc_event *s_nbdram, sc_event *e_nbdram, NB_dcachecore *nb_dcache)
        : mau(mau), hmau(hmau), msg_data(msg_data), sram_addr(sram_addr), s_nbdram(s_nbdram), e_nbdram(e_nbdram), nb_dcache(nb_dcache) {}
    TaskCoreContext(mem_access_unit *mau, high_bw_mem_access_unit *hmau, const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr, sc_event *s_nbdram, sc_event *e_nbdram, NB_dcachecore *nb_dcache,
                    int loop_cnt)
        : mau(mau), hmau(hmau), msg_data(msg_data), sram_addr(sram_addr), s_nbdram(s_nbdram), e_nbdram(e_nbdram), nb_dcache(nb_dcache), loop_cnt(loop_cnt) {}
#else
    // 构造函数
    TaskCoreContext(DcacheCore *wc, mem_access_unit *mau, high_bw_mem_access_unit *hmau, const sc_bv<SRAM_BITWIDTH> &msg_data, int *sram_addr, sc_event *s_nbdram, sc_event *e_nbdram)
        : wc(wc), mau(mau), hmau(hmau), msg_data(msg_data), sram_addr(sram_addr), s_nbdram(s_nbdram), e_nbdram(e_nbdram) {}
#endif
};