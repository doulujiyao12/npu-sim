#pragma once
#include "systemc.h"

#include "prims/gpu_base.h"
#include "prims/pd_base.h"

class Matmul_f_gpu : public gpu_base {
public:
    int B, T, C, OC;
    int slice_x, slice_y;

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
    int slice_x, slice_y;

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
    int slice_x, slice_y;

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
    int slice_x, slice_y;

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
    int slice_x, slice_y;

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


class matmul_forward_gpu_pd : public gpu_base {
public:
    int B, T, C, OC;
    int NH, DH, R;
    PD_JOB job_type = JOB_BOTH;
    int slice_x, slice_y;

    vector<Stage> batchInfo;
    vector<bool> *decode_done;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);

    gpu_base *clone() { return new matmul_forward_gpu_pd(*this); }

    matmul_forward_gpu_pd() { name = "matmul_forward_gpu_pd"; }
};

class attention_forward_gpu_pd : public gpu_base {
public:
    int B, T, C, NH;
    int slice_x, slice_y;

    vector<Stage> batchInfo;
    vector<bool> *decode_done;

    int task_core(TaskCoreContext &context);
    int task();
    int sram_utilization(DATATYPE datatype, int cid = 0);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void print_self(string prefix);
    void parse_json(json j);

    gpu_base *clone();

    attention_forward_gpu_pd() { name = "attention_forward_gpu_pd"; }
};