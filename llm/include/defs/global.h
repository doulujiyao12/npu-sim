#pragma once
#include "defs/enums.h"
#include "macros/macros.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

using namespace std;

// 全局的原语数组
class prim_base;
extern vector<prim_base *> global_prim_stash;

// 模拟KVcache（已过时）
class KVCache;
extern KVCache KVCache_g;

// one per system，用于config转msg的消息传递
class AddrLabelTable;
extern AddrLabelTable g_addr_label_table;


// 记录所有在计算原语中的参数，见test文件夹下的config文件
extern vector<pair<string, int>> vtable;

extern u_int64_t data_footprint_in_words;

// 网络拓扑大小
extern int GRID_X;
extern int GRID_Y;
extern int GRID_SIZE;
extern int CORE_PER_SM;
// extern int BOARD_W;

// 模拟模式（数据流/gpu）
extern SIM_MODE SYSTEM_MODE;

// 模拟dram数组
#if DUMMY == 1
extern uint32_t *dram_array;
#else
// used for DRAM on cores
// uint32_t *dram_array[GRID_SIZE];
#endif

// dcache相关
#if DCACHE == 1
// u_int16_t * dcache_freq;
extern std::unordered_map<u_int64_t, u_int16_t> dcache_freq_v2;
extern bool *dcache_dirty;
extern uint64_t **dcache_tags;
extern uint32_t *dcache_occupancy;
extern uint32_t *dcache_last_evicted;
#endif

extern u_int64_t dcache_hits;
extern u_int64_t dcache_misses;
extern u_int64_t dcache_evictions;
extern u_int64_t *mc_transactions;
extern u_int64_t *mc_latency;
extern u_int64_t *mc_writebacks;
extern u_int32_t ***frame_counters;


// used for worker core and router
enum worker_core_state { CORE_IDLE = 0, CORE_TASK = 1, CORE_DATA = 2, CORE_RECEIVE = 3 };

extern bool use_node;
extern bool use_DramSys;
extern float comp_util;

#define RESET "\033[0m"  // 重置颜色
#define RED "\033[31m"   // 红色
#define GREEN "\033[32m" // 绿色

enum Etype { MAC_Array };

typedef struct {
    Etype type; // exu type
    int x_dims; // exu x array
    int y_dims; // exu y array
} ExuConfig;

extern ExuConfig tile_exu;

enum Sftype { Linear };

typedef struct {
    Sftype type; // exu type
    int x_dims;  // exu x array
} SfuConfig;

extern SfuConfig tile_sfu;
