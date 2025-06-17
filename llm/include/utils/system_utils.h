#pragma once
#include <iostream>
#include <string>
#include <utility>
#include "common/config.h"

using namespace std;

bool rand_result(int threshold);

// 在全局vtable中查找对应名字的参数
int find_var(string var);

ExuConfig *get_exu_config(int id);
SfuConfig *get_sfu_config(int id);
int get_sram_bitwidth(int id);
string get_dram_config(int id);

int ceiling_division(int a, int b);

string send_prim_type_to_string(int type);
string recv_prim_type_to_string(int type);

void init_grid(string config_path);
void init_global_members();
void system_cleanup();

void initialize_cache_structures();
void init_perf_counters();
void destroy_cache_structures();
void init_dram_areas();
void destroy_dram_areas();

// mesh的结构，编号是z字型编号的 先x宽度方向编号
int global(int x, int y);

// 在内存层级中使用
uint64_t get_bank_index(uint64_t address);

// 打印系统配置
void print_configuration(ostream &fout);

// split字符串
std::vector<std::string> split(const std::string &s, char delimiter);

#define BETTER_PRINT(var) do {std::cout << __PRETTY_FUNCTION__ << " print: " << #var << ": " << (var) << std::endl;} while (0)