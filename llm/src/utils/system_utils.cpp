#include "systemc.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#include "defs/const.h"
#include "defs/enums.h"
#include "defs/global.h"
#include "utils/config_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

bool RandResult(int threshold) {
    int num = rand() % 100 + 1;
    return num <= threshold;
}

int GetFromPairedVector(vector<pair<string, int>> &vector, string key) {
    for (auto &pair : vector) {
        if (pair.first == key)
            return pair.second;
    }

    ARGUS_EXIT("Key ", key, " does not exist in vector.\n");
    return -1;
}

CoreHWConfig GetCoreHWConfig(int id) {
    for (auto pair : g_core_hw_config) {
        if (pair.first == id)
            return pair.second;
    }

    ARGUS_EXIT("Core HW config for id ", id, " does not exist.\n");
    return CoreHWConfig();
}

int CeilingDivision(int a, int b) {
    if (b == 0) {
        ARGUS_EXIT("Division by zero.\n");
    }

    return (a + b - 1) / b;
}

void InitGrid(string config_path, string core_config_path) {
    json j1;
    ifstream jfile1(config_path);

    if (!jfile1.is_open())
        ARGUS_EXIT("Failed to open file ", config_path, ".\n");

    jfile1 >> j1;
    ParseSimulationType(j1);

    json j2;
    ifstream jfile2(core_config_path);

    if (!jfile2.is_open())
        ARGUS_EXIT("Failed to open file ", core_config_path, ".\n");

    jfile2 >> j2;
    ParseHardwareConfig(j2);
}

void SystemCleanup() {
    // 清理所有原语
    for (auto p : g_prim_stash) 
        delete p;

    delete[] dram_array;
#if DCACHE == 1
    delete[] dcache_tags;
    delete[] dcache_occupancy;
    delete[] dcache_last_evicted;
#endif
}

void InitGlobalMembers() {
#if DUMMY == 1
    dram_array = new uint32_t[GRID_SIZE];
#endif

#if DCACHE == 1
    dcache_tags = new uint64_t *[GRID_SIZE];
    dcache_occupancy = new uint32_t[GRID_SIZE];
    dcache_last_evicted = new uint32_t[GRID_SIZE];
#endif
}

// void initialize_cache_structures() {
// #if DCACHE == 1
//     // data_footprint_in_words = GRID_SIZE * dataset_words_per_tile; //global
//     // variable 全局的darray的大小 所有的tile

//     data_footprint_in_words =
//         GRID_SIZE * dataset_words_per_tile; // global variable
//     printf("GRID_SIZE%d, ss%ld\n", GRID_SIZE, dataset_words_per_tile);
//     assert(data_footprint_in_words > 0);

//     // u_int64_t total_lines =
//     //     data_footprint_in_words >> dcache_words_in_line_log2;
//     u_int64_t total_lines = dataset_words_per_tile >>
//     dcache_words_in_line_log2; printf("dataset_words_per_tile %ld \n",
//     dataset_words_per_tile); printf("data_footprint_in_words %ld \n",
//     data_footprint_in_words); printf("total_lines %ld \n", total_lines);
//     // dcache_freq = (u_int16_t *)calloc(total_lines, sizeof(u_int16_t));
//     // dcache_dirty = (bool *)calloc(total_lines, sizeof(bool));
//     dcache_dirty =
//         new std::unordered_set<uint64_t>[GRID_SIZE]; // One set per tile

//     // dcache_size dcache size of each tile
//     u_int64_t lines_per_tile = dcache_size >> dcache_words_in_line_log2;
//     for (int i = 0; i < GRID_SIZE; i++) {
//         u_int64_t *array_uint =
//             (u_int64_t *)calloc(lines_per_tile, sizeof(u_int64_t));
//         // bool *dcache_dirty = (bool *)calloc(total_lines, sizeof(bool));
//         for (int j = 0; j < lines_per_tile; j++) {
//             array_uint[j] = UINT64_MAX;
//         }
//         dcache_tags[i] = array_uint;
//         dcache_occupancy[i] = 0;
//         dcache_last_evicted[i] = 0;
//     }
// #endif
// }

// void init_dram_areas() {
//     for (int i = 0; i < GRID_SIZE; i++) {
// #if DUMMY == 1
// #else
//         dram_array[i] =
//             (uint32_t *)calloc(dataset_words_per_tile, sizeof(uint32_t));
//         if (dram_array[i] == NULL)
//             std::cout << "Failed to calloc dram.\n";
// #endif
//     }

//     for (int i = 0; i < GRID_SIZE; i++) {
// #if DUMMY == 1
// #else
//         float *a = (float *)(dram_array[i]);
//         for (int j = 0; j < dataset_words_per_tile; j++)
//             a[j] = 1;
// #endif
//     }
// }

// void destroy_dram_areas() {
//     // free all of the dram areas
// #if DUMMY == 1
// #else
//     for (int i = 0; i < GRID_X; i++) {
//         free(dram_array[i]);
//         dram_array[i] = NULL;
//     }
// #endif
//     std::cout << "DRAM destroyed.\n";
// }
