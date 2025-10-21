#pragma once
#include <string>

// 配置文件
extern std::string g_workload_config;
extern std::string g_hardware_config;
extern std::string g_simulation_config;

// simulation config中包含的参数
extern bool SPEC_USE_BEHA_NOC;
extern bool SPEC_USE_BEHA_SRAM;
extern bool SPEC_USE_BEHA_DRAM;
extern bool SPEC_USE_PERF_GEMM;
extern bool SPEC_KVCACHE_SPILL;
extern bool SPEC_LOAD_STATIC_AS_TILE;
extern std::string SPEC_TTF_FILE;
extern bool SPEC_USE_DRAMSYS;
extern bool SPEC_FAST_WARMUP;

// hardware config中包含的参数
extern int HW_CORE_CREDIT;
extern int HW_PD_RATIO;
extern int HW_DRAM_BITWIDTH;
extern int HW_SRAM_SIZE;
extern int HW_NOC_PAYLOAD_PER_CYCLE;