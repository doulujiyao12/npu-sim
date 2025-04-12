#pragma once
#include "systemc.h"

#include "prims/gpu_base.h"

class Matmul_f_gpu : public gpu_base {
public:
    int B, T, C, OC;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);
    void initialize();
    gpu_base *clone();
};