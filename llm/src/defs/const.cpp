#include "defs/const.h"
#include "macros/macros.h"

const double pu_freq = 1.0;
const double mem_freq = 1.0;
const double noc_freq = 1.0;
const double pu_mem_ratio = pu_freq / mem_freq;
const double noc_to_pu_ratio = pu_freq / noc_freq;

u_int64_t dataset_words_per_tile = 1024 * 1024 * 16;
u_int32_t dcache_size = 128;
u_int32_t dcache_words_in_line_log2 = 4; // 16 words, i.e., 64B

u_int32_t hbm_channels = 8;
u_int32_t total_hbm_channels = hbm_channels * DIES;

bool dataset_cached = true;

u_int32_t dcache_lines = dcache_size >> dcache_words_in_line_log2;
u_int32_t dcache_sets = (dcache_lines >> CACHE_WAY_BITS);

#if BANK_DEPTH > 524288
u_int32_t sram_read_latency = ceil_macro(3 * pu_mem_ratio * CYCLE);
#elif BANK_DEPTH > 131072
u_int32_t sram_read_latency = ceil_macro(2 * pu_mem_ratio * CYCLE);
#else
u_int32_t sram_read_latency = ceil_macro(1 * pu_mem_ratio * CYCLE);
#endif

u_int32_t hbm_device_latency = ceil_macro(30 * pu_mem_ratio);
const u_int32_t average_die_to_edge_distance = hops_to_mc(DIE_W);
u_int32_t on_die_hop_latency = 1;

u_int32_t hbm_read_latency =
    hbm_device_latency +
    ceil_macro(noc_to_pu_ratio * average_die_to_edge_distance *
               on_die_hop_latency);

// ---------------------------------------------------------

const int T_CLOCK_CYClE = 2; // NS

// global
const int MEM_PORT_WIDTH = 256; // bits

// architecture
const int DRAM_DEPTH_PER_CORE = 786432;
const int GLOBAL_BUFFER_DEPTH_PER_CORE = 8192;
const int PRIM_BUFFER_DEPTH_PER_CORE = 1024;

// memory
int RAM_READ_LATENCY = sram_read_latency;  // NS
int RAM_WRITE_LATENCY = sram_read_latency; // NS
const double RAM_STATIC_POWER = 0.001;
const double RAM_READ_ENERGY = 0.1;
const double RAM_WRITE_ENERGY = 0.2;

const int DRAM_READ_LATENCY = 50;       // NS
const int DRAM_WRITE_LATENCY = 50;      // NS
const int DRAM_BURST_WRITE_LATENCY = 3; // NS
const int DRAM_BURST_READ_LATENCY = 3;  // NS
const double DRAM_REFRESH_POWER = 0.1;
const int DRAM_READ_ENERGY = 10;
const int DRAM_WRITE_ENERGY = 20;
const int DRAM_TRANSFER_ENERGY = 50;
const int DRAM_LINE = 10;
const double DRAM_ENERGY_FACTOR = 0.9;

// control unit
const int ROM_START_PC = 0;

// NoC
const int DATA_WIDTH = 8; // Byte

// PE unit
const int I_BYTE = 32;

// Visual
const int Men_usage_thre = 2;