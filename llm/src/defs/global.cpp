#include "defs/global.h"
#include "common/memory.h"
#include "defs/enums.h"
#include <systemc>

u_int64_t dcache_hits = 0;
u_int64_t dcache_misses = 0;
u_int64_t dcache_evictions = 0;

class ExuConfig;
class SfuConfig;
vector<pair<int, ExuConfig *>> tile_exu;
vector<pair<int, SfuConfig *>> tile_sfu;
vector<pair<int, int>> mem_sram_bw; // 用于记录每个核的sram bitwidth 
vector<pair<int, string>> mem_dram_config_str; // 用于记录每个核的dram配置文件名

class prim_base;
vector<prim_base *> global_prim_stash;
vector<chip_instr_base*> global_chip_prim_stash;

KVCache KVCache_g;
AddrLabelTable g_addr_label_table;

DramKVTable** g_dram_kvtable;
int MAX_SRAM_SIZE;
sc_event kv_event;
int dram_aligned;
bool use_gpu;

int CORE_COMM_PAYLOAD = 1; // 一个时钟周期可以一次性发送多少数据包

string gpu_dram_config;

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
std::unordered_set<uint64_t> *dcache_dirty;
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
bool gpu_inner;
float comp_util;
bool gpu_clog;
int gpu_bw;
string g_config_file;
int dram_bw;
// int DRAM_BURST_BYTE;
// int L1CACHELINESIZE;
// int L2CACHELINESIZE;

// 网络拓扑大小
int GRID_X;
int GRID_Y;
int GRID_SIZE;
int CORE_PER_SM;

// 模拟模式（数据流/gpu）
SIM_MODE SYSTEM_MODE = SIM_DATAFLOW;

int verbose_level;
std::unordered_map<int, std::ofstream*> log_streams;

const char* get_core_color(int core_id) {

    static const char* colors[] = {
        "\033[31m", // 红色
        "\033[32m", // 绿色
        "\033[33m", // 黄色
        "\033[34m", // 蓝色
        "\033[35m", // 品红
        "\033[36m", // 青色
        "\033[91m", // 亮红
        "\033[92m", // 亮绿
    };
    return colors[core_id % (sizeof(colors)/sizeof(colors[0]))];
}

void log_verbose_impl(int level, int core_id, const std::string& message) {
    if (verbose_level >= level) {
        std::ostringstream oss;
        
#if ENABLE_COLORS == 1
        oss << get_core_color(core_id)
            << "[INFO] Core " << core_id << " " << message << " " << sc_time_stamp().to_string()
             << "\033[0m";
#else
        
        oss << "[INFO] Core " << core_id << " " << message << " " << sc_time_stamp().to_string();
#endif
        std::string log_msg = oss.str();

        // 控制台输出
        std::cout << log_msg << std::endl;

        // 文件输出
        auto it = log_streams.find(core_id);
        if (it == log_streams.end()) {
            std::string filename = "core_" + std::to_string(core_id) + ".log";
            log_streams[core_id] = new std::ofstream(filename, std::ios::app);
            if (*log_streams[core_id])
                *log_streams[core_id] << "-- New Session --\n";
        }
        if (log_streams[core_id] && log_streams[core_id]->is_open()) {
            *log_streams[core_id] << log_msg << std::endl;
        }
    }
}

void close_log_files() {
    for (auto& pair : log_streams) {
        if (pair.second && pair.second->is_open()) {
            pair.second->close();
            delete pair.second;
        }
    }
    log_streams.clear();
}
