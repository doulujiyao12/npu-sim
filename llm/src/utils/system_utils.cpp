#include <fstream>
#include <cstdlib>
#include "systemc.h"

#include "defs/const.h"
#include "defs/enums.h"
#include "defs/global.h"
#include "utils/system_utils.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

bool rand_result(int threshold) {
    int num = rand() % 100 + 1;
    return num <= threshold;
}

// 在全局vtable中查找对应名字的参数
int find_var(string var) {
    for (auto v : vtable) {
        if (v.first == var)
            return v.second;
    }

    cout << "Check config settings, unknown variable: " << var << endl;
    return 1;
}

void set_var_gpt2(int B, int T, int C, int NH) {
    vtable.clear();
    vtable.push_back(make_pair("B", B));
    vtable.push_back(make_pair("T", T));
    vtable.push_back(make_pair("C", C));
    vtable.push_back(make_pair("NH", NH));
    vtable.push_back(make_pair("3C", 3*C));
    vtable.push_back(make_pair("4C", 4*C));
    vtable.push_back(make_pair("BTC", B*T*C));
    vtable.push_back(make_pair("2BTC", 2*B*T*C));
    vtable.push_back(make_pair("3BTC", 3*B*T*C));
    vtable.push_back(make_pair("4BTC", 4*B*T*C));
}

int ceiling_division(int a, int b) {
    if (b == 0) {
        throw std::invalid_argument("Division by zero is not allowed.");
    }
    // 上述公式： (a + b - 1) / b
    int result;
    result = (a + b - 1) / b;
    return result;
}

string send_prim_type_to_string(int type) {
    switch (type) {
    case SEND_ACK:
        return "SEND_ACK";
    case SEND_DONE:
        return "SEND_DONE";
    case SEND_DATA:
        return "SEND_DATA";
    case SEND_SRAM:
        return "SEND_SRAM";
    case SEND_REQ:
        return "SEND_REQ";
    default:
        return "UNKNOWN type " + to_string(type);
    }
}

string recv_prim_type_to_string(int type) {
    switch (type) {
    case RECV_ACK:
        return "RECV_ACK";
    case RECV_CONF:
        return "RECV_CONF";
    case RECV_DATA:
        return "RECV_DATA";
    case RECV_FLAG:
        return "RECV_FLAG";
    case RECV_SRAM:
        return "RECV_SRAM";
    case RECV_WEIGHT:
        return "RECV_WEIGHT";
    case RECV_START:
        return "RECV_START";
    default:
        return "UNKNOWN type: " + to_string(type);
    }
}

void init_grid(string config_path) {
    json j;
    ifstream jfile(config_path);
    jfile >> j;

    if (j.contains("mode")) {
        auto mode = j["mode"];

        if (mode == "dataflow") {
            SYSTEM_MODE = SIM_DATAFLOW;
        } else if (mode == "gpu") {
            SYSTEM_MODE = SIM_GPU;
            if (USE_L1L2_CACHE != 1) {
                cout << "Please activate \'USE_L1L2_CACHE\' macro.";
                sc_stop();
            }
        } else if (mode == "sched_pd") {
            SYSTEM_MODE = SIM_PD;
        }
    } else {
        SYSTEM_MODE = SIM_DATAFLOW;
    }

    if (j.contains("chips")) {
        GRID_X = j["chips"][0]["GridX"];
        GRID_Y = GRID_X;
        GRID_SIZE = GRID_X * GRID_Y;

        if (SYSTEM_MODE == SIM_GPU) {
            CORE_PER_SM = j["chips"][0]["core_per_sm"];
        }
    }
}

void init_global_members() {
#if DUMMY == 1
    dram_array = new uint32_t[GRID_SIZE];
#endif

#if DCACHE
    dcache_tags = new uint64_t *[GRID_SIZE];
    dcache_occupancy = new uint32_t[GRID_SIZE];
    dcache_last_evicted = new uint32_t[GRID_SIZE];
#endif
}

void initialize_cache_structures() {
#if DCACHE == 1
    // data_footprint_in_words = GRID_SIZE * dataset_words_per_tile; //global
    // variable 全局的darray的大小 所有的tile

    data_footprint_in_words = GRID_SIZE * dataset_words_per_tile; // global variable
    printf("GRID_SIZE%d, ss%ld\n", GRID_SIZE, dataset_words_per_tile);
    assert(data_footprint_in_words > 0);

    u_int64_t total_lines = data_footprint_in_words >> dcache_words_in_line_log2;
    printf("dataset_words_per_tile %ld \n", dataset_words_per_tile);
    printf("data_footprint_in_words %ld \n", data_footprint_in_words);
    printf("total_lines %ld \n", total_lines);

    // printf("WARNING dataset_words_per_tile can not be set larger than host
    // memeory");

    // dcache_freq = (u_int16_t *)calloc(total_lines, sizeof(u_int16_t));
    dcache_dirty = (bool *)calloc(total_lines, sizeof(bool));

    // dcache 是在片上的给到硬件dcache的大小
    u_int64_t lines_per_tile = dcache_size >> dcache_words_in_line_log2;
    for (int i = 0; i < GRID_SIZE; i++) {
        u_int64_t *array_uint = (u_int64_t *)calloc(lines_per_tile, sizeof(u_int64_t));
        for (int j = 0; j < lines_per_tile; j++) {
            array_uint[j] = UINT64_MAX;
        }
        dcache_tags[i] = array_uint;
        dcache_occupancy[i] = 0;
        dcache_last_evicted[i] = 0;
    }
#endif
}


void init_perf_counters() {
    // 每个die有一组hbm_channel,这里是所有的die加起来的hbm_channel的数量
    // u_int32_t total_hbm_channels = hbm_channels * DIES;
    mc_transactions = (u_int64_t *)calloc(total_hbm_channels, sizeof(u_int64_t));
    mc_latency = (u_int64_t *)calloc(total_hbm_channels, sizeof(u_int64_t));

    // total_counters = new u_int64_t**[GRID_X];
    frame_counters = new u_int32_t **[GRID_X];
    for (int i = 0; i < GRID_X; i++) {
        // total_counters[i] = new u_int64_t*[GRID_Y];
        frame_counters[i] = new u_int32_t *[GRID_Y];
        for (int j = 0; j < GRID_Y; j++) {
            // total_counters[i][j] = new u_int64_t[GLOBAL_COUNTERS];
            frame_counters[i][j] = new u_int32_t[GLOBAL_COUNTERS];
            for (int c = 0; c < GLOBAL_COUNTERS; c++) {
                // total_counters[i][j][c] = 0;
                frame_counters[i][j][c] = 0;
            }
        }
    }
}

void destroy_cache_structures() {
    free(mc_transactions);
    free(mc_latency);
#if DCACHE
    // free(dcache_freq);
    free(dcache_dirty);

    for (int i = 0; i < GRID_SIZE; i++) {
        free(dcache_tags[i]);
    }

    // free(dcache_tags);
#endif
    for (u_int32_t i = 0; i < GRID_X; i++) {
        for (u_int32_t j = 0; j < GRID_Y; j++) {
            delete frame_counters[i][j];
        }
        delete frame_counters[i];
    }
}

void init_dram_areas() {
    // malloc a vector of fixed areaa for each tile as the DRAM area
    // WEAK: we now assume there are only 1*GRID_X tiles
    for (int i = 0; i < GRID_SIZE; i++) {
#if DUMMY == 1
#else
        dram_array[i] = (uint32_t *)calloc(dataset_words_per_tile, sizeof(uint32_t));
        if (dram_array[i] == NULL)
            std::cout << "Failed to calloc dram.\n";
#endif
    }

    for (int i = 0; i < GRID_SIZE; i++) {
#if DUMMY == 1
#else
        float *a = (float *)(dram_array[i]);
        for (int j = 0; j < dataset_words_per_tile; j++)
            a[j] = 1;
#endif
    }
}

void destroy_dram_areas() {
    // free all of the dram areas
#if DUMMY == 1
#else
    for (int i = 0; i < GRID_X; i++) {
        free(dram_array[i]);
        dram_array[i] = NULL;
    }
#endif
    std::cout << "DRAM destroyed.\n";
}

void system_cleanup() {
    // 清理所有原语
    for (auto p : global_prim_stash) {
        delete p;
    }

    delete[] dram_array;
    delete[] dcache_tags;
    delete[] dcache_occupancy;
    delete[] dcache_last_evicted;
}

// mesh的结构，编号是z字型编号的
// 先x 宽度方向编号
int global(int x, int y) { return (((y % 2) == 1) ? (y * GRID_X) + (GRID_X - 1) - x : (y * GRID_X) + x); }

// 在内存层级中使用
uint64_t get_bank_index(uint64_t address) {
    uint64_t column_idx = address % SRAM_BANKS;
    return column_idx;
}

// 打印系统配置
void print_configuration(ostream &fout) {
    // Print with format thousands separator
    fout.imbue(locale(""));

    // Print Configuration Run
    fout << "GRID_X: " << GRID_X << endl;
    fout << "GRID_SIZE: " << GRID_X * GRID_Y << endl;
    fout << "PROXY_W: " << PROXY_W << endl;
    fout << "BOARD_W: " << BOARD_W << endl;
    fout << "PACK_W: " << PACK_W << endl;
    fout << "DIE_W: " << DIE_W << endl;
}