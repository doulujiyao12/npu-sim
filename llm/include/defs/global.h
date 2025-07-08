#pragma once
#include "defs/enums.h"
#include "macros/macros.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "../unit_module/dram_kvtable/dram_kvtable.h"
#include <unordered_set>

using namespace std;

// 全局的原语数组
class prim_base;
class chip_instr_base;
extern vector<prim_base *> global_prim_stash;
extern vector<chip_instr_base *> global_chip_prim_stash;

extern int MAX_SRAM_SIZE;

class DramKVTable;
extern DramKVTable** g_dram_kvtable;

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

// 模拟模式（数据流/gpu/pd serving）
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

extern std::unordered_set<uint64_t> *dcache_dirty;
// extern bool **dcache_dirty;
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

extern bool use_node;
extern bool use_DramSys;
extern float comp_util;

#define RESET "\033[0m"  // 重置颜色
#define RED "\033[31m"   // 红色
#define GREEN "\033[32m" // 绿色

class ExuConfig;
class SfuConfig;
extern vector<pair<int, ExuConfig *>> tile_exu;
extern vector<pair<int, SfuConfig *>> tile_sfu;
extern vector<pair<int, int>> mem_sram_bw;
extern vector<pair<int, string>> mem_dram_config_str;