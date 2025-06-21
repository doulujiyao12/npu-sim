#pragma once
#include "systemc.h"

#include "prims/gpu_base.h"

class Matmul_f_gpu : public gpu_base {
public:
    int B, T, C, OC;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    Matmul_f_gpu() { name = "Matmul_f_gpu"; }
};

class Attention_f_gpu : public gpu_base {
public:
    int B, T, C, NH;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    Attention_f_gpu() { name = "Attention_f_gpu"; }
};

class Gelu_f_gpu : public gpu_base {
public:
    int N;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    Gelu_f_gpu() { name = "Gelu_f_gpu"; }
};

class Layernorm_f_gpu : public gpu_base {
public:
    int B, T, C;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    Layernorm_f_gpu() { name = "Layernorm_f_gpu"; }
};


class Residual_f_gpu : public gpu_base {
public:
    int N;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    Residual_f_gpu() { name = "Residual_f_gpu"; }
};