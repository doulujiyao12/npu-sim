#pragma once
#include "macros/macros.h"
#include <iostream>

extern const double pu_freq;
extern const double mem_freq;
extern const double noc_freq;
extern const double pu_mem_ratio;
extern const double noc_to_pu_ratio;

// 一个tile中的dram大小
extern u_int64_t dataset_words_per_tile;

// 物理cache的大小，是实际上在sram上面cache的sram的大小
extern u_int32_t dcache_size;
extern u_int32_t dcache_words_in_line_log2;

extern u_int32_t hbm_channels;
extern u_int32_t total_hbm_channels;

extern bool dataset_cached;

extern u_int32_t dcache_lines;
extern u_int32_t dcache_sets;

// PU pipeline stalls from reading the SRAM (after pipelining)
#if BANK_DEPTH > 524288
extern u_int32_t sram_read_latency;
#elif BANK_DEPTH > 131072
extern u_int32_t sram_read_latency;
#else
extern u_int32_t sram_read_latency;
#endif

// Latency inside the HBM from the point of view of the PU
extern u_int32_t hbm_device_latency;
extern const u_int32_t average_die_to_edge_distance;
extern u_int32_t on_die_hop_latency; // in cycles of noc clk

extern u_int32_t hbm_read_latency;

// ---------------------------------------------------------

// 以下是mem_config相关
extern const int T_CLOCK_CYClE; // NS

// global
extern const int MEM_PORT_WIDTH; // bits

// architecture
extern const int DRAM_DEPTH_PER_CORE;
extern const int GLOBAL_BUFFER_DEPTH_PER_CORE;
extern const int PRIM_BUFFER_DEPTH_PER_CORE;

// memory
extern int RAM_READ_LATENCY;  // NS
extern int RAM_WRITE_LATENCY; // NS
extern const double RAM_STATIC_POWER;
extern const double RAM_READ_ENERGY;
extern const double RAM_WRITE_ENERGY;

extern const int DRAM_READ_LATENCY;        // NS
extern const int DRAM_WRITE_LATENCY;       // NS
extern const int DRAM_BURST_WRITE_LATENCY; // NS
extern const int DRAM_BURST_READ_LATENCY;  // NS
extern const double DRAM_REFRESH_POWER;
extern const int DRAM_READ_ENERGY;
extern const int DRAM_WRITE_ENERGY;
extern const int DRAM_TRANSFER_ENERGY;
extern const int DRAM_LINE;
extern const double DRAM_ENERGY_FACTOR;

// control unit
extern const int ROM_START_PC;

// NoC
extern const int DATA_WIDTH; // Byte

// PE unit
extern const int I_BYTE;

// Visual
extern const int Men_usage_thre;
