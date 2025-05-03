#pragma once
#include <string>

using namespace std;

bool rand_result(int threshold);

// 在全局vtable中查找对应名字的参数
int find_var(string var);
void set_var_gpt2(int B, int T, int C, int NH);

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
