#pragma once
#include <iostream>
#include <string>
#include <utility>
#include "common/config.h"

using namespace std;

bool RandResult(int threshold);
int GetFromPairedVector(vector<pair<string, int>> &vector, string key);

CoreHWConfig *GetCoreHWConfig(int id);

int CeilingDivision(int a, int b);

void InitGrid(string config_path, string core_config_path);
void InitGlobalMembers();
void SystemCleanup();

// void initialize_cache_structures();
// void init_dram_areas();
// void destroy_dram_areas();