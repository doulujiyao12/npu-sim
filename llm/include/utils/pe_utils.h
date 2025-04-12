#pragma once

// 此文件用于脉动阵列相关的工具函数

bool PE_is_sum_start(int peid);
bool PE_is_weight_end(int peid);
bool PE_is_data_end(int peid);

void test_fill_data(int batch, int count, ...);