#pragma once
#include "common/config.h"
#include <iostream>
#include <string>
#include <utility>

using namespace std;

bool RandResult(int threshold);
int GetFromPairedVector(vector<pair<string, int>> &vector, string key);
CoreHWConfig *GetCoreHWConfig(int id);
int CeilingDivision(int a, int b);

void InitGrid(string workload_config_path, string hardware_config_path,
              string simulation_config_path);
void InitGlobalMembers();
void SystemCleanup();

void DeleteCoreLogFiles();
void DeleteMemoryLogFiles();

// void initialize_cache_structures();
// void init_dram_areas();
// void destroy_dram_areas();