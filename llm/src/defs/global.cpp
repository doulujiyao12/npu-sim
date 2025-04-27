#include "defs/global.h"
#include "common/memory.h"
#include "defs/enums.h"

u_int64_t dcache_hits = 0;
u_int64_t dcache_misses = 0;
u_int64_t dcache_evictions = 0;

ExuConfig tile_exu = {MAC_Array, 16, 16};
SfuConfig tile_sfu = {Linear, 16};

class prim_base;
vector<prim_base *> global_prim_stash;

KVCache KVCache_g;
AddrLabelTable g_addr_label_table;

// 记录所有在计算原语中的参数，见test文件夹下的config文件
vector<pair<string, int>> vtable;

u_int64_t data_footprint_in_words;

#if DUMMY == 1
uint32_t *dram_array;
#else
// used for DRAM on cores
// uint32_t *dram_array[GRID_SIZE];
#endif

#if DCACHE == 1
// u_int16_t * dcache_freq;
std::unordered_map<u_int64_t, u_int16_t> dcache_freq_v2;
bool *dcache_dirty;
uint64_t **dcache_tags;
uint32_t *dcache_occupancy;
uint32_t *dcache_last_evicted;
#endif

u_int64_t *mc_transactions;
u_int64_t *mc_latency;
u_int64_t *mc_writebacks;
u_int32_t ***frame_counters;

bool use_node;
bool use_DramSys;
float comp_util;

// 网络拓扑大小
int GRID_X;
int GRID_Y;
int GRID_SIZE;
int CORE_PER_SM;

// 模拟模式（数据流/gpu）
SIM_MODE SYSTEM_MODE = SIM_DATAFLOW;
