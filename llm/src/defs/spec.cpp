#include "defs/spec.h"

std::string g_workload_config;
std::string g_hardware_config;
std::string g_simulation_config;

bool SPEC_USE_BEHA_NOC = true;
bool SPEC_USE_BEHA_SRAM = true;
bool SPEC_USE_BEHA_DRAM = true;
bool SPEC_USE_PERF_GEMM = true;
bool SPEC_KVCACHE_SPILL = false;
bool SPEC_LOAD_STATIC_AS_TILE = false;
std::string SPEC_TTF_FILE = "../font/NotoSansDisplay-Bold.ttf";
bool SPEC_USE_DRAMSYS = true;
bool SPEC_FAST_WARMUP = true;

int HW_CORE_CREDIT = 5;
int HW_PD_RATIO = 4;
int HW_DRAM_BITWIDTH = 32;
int HW_SRAM_SIZE = 8388608;
int HW_NOC_PAYLOAD_PER_CYCLE = 1;