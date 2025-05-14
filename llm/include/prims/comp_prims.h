#pragma once
#include "systemc.h"

#include "common/system.h"
#include "prims/comp_base.h"

class Attention_f_decode : public comp_base {
public:
    int B, T, C, NH;
    int prea_offset, a_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Attention_f_decode() { name = "Attention_f_decode"; }
};


class Attention_f : public comp_base {
public:
    int B, T, C, NH;
    int prea_offset, a_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Attention_f() { name = "Attention_f"; }
};


class Batchnorm_f : public comp_base {
public:
    int B, H, W, C;
    int gamma_offset, beta_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Batchnorm_f() { name = "Batchnorm_f"; }
};


class Conv_f : public comp_base {
public:
    int inp_offset; // 16 16
    int out_offset;

    int B, W, H, C;     // 4 16 16 8
    int pX, pY, sX, sY; // 8 8 4 4
    int kX, kY, F;      // 8 8 4

    int oW, oH, oC; // 通过计算可以得到
    int k_offset, b_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    Conv_f() { name = "Conv_f"; }
};


class Dummy_p : public comp_base {
public:
    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Dummy_p() { name = "Dummy_p"; }
};


class Gelu_f : public comp_base {
public:
    int N;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Gelu_f() { name = "Gelu_f"; }
};


class Layernorm_f : public comp_base {
public:
    int B, T, C;
    int w_offset, b_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    Layernorm_f() { name = "Layernorm_f"; }
};


class Matmul_f_decode : public comp_base {
public:
    int B, T, C, OC;
    int w_offset, b_offset;

    DATATYPE datatype;


    int task();
    int task_r();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    HardwareTaskConfig *generate_hw_config();
    Matmul_f_decode() { name = "Matmul_f_decode"; }

    void matmul_forward_naive(float *out, const float *inp, const float *weight,
                              const float *bias, int B, int T, int C, int OC);
};


class Matmul_f_prefill : public comp_base {
public:
    int B, T, C, OC;
    int w_offset, b_offset;

    int task();
    int task_r();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    HardwareTaskConfig *generate_hw_config();
    Matmul_f_prefill() { name = "Matmul_f_prefill"; }

    void matmul_forward_naive(float *out, const float *inp, const float *weight,
                              const float *bias, int B, int T, int C, int OC);
};


class Matmul_f : public comp_base {
public:
    int B, T, C, OC;
    int w_offset, b_offset;

    int task();
    int task_r();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    HardwareTaskConfig *generate_hw_config();
    Matmul_f() { name = "Matmul_f"; }

    void matmul_forward_naive(float *out, const float *inp, const float *weight,
                              const float *bias, int B, int T, int C, int OC);
};

class switch_data : public comp_base {
public:
    int IN, OUT;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    switch_data() { name = "switch_data"; }
};


class Max_pool : public comp_base {
public:
    int inp_offset; // 16 16
    int out_offset;

    int B, W, H, C;     // 4 16 16 8
    int pX, pY, sX, sY; // 8 8 4 4
    int kX, kY;         // 8 8 4

    int oW, oH, oC; // 通过计算可以得到
    int k_offset, b_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    Max_pool() { name = "Max_pool"; }
};


class Merge_conv : public comp_base {
public:
    int B, T, C;

    int dim; // 1: concat, 2: add
    int slice;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void parse_matmul(Matmul_f *p);
    Merge_conv() { name = "Merge_conv"; }
};


class Merge_matmul : public comp_base {
public:
    int B, T, C;

    int dim; // 1: concat, 2: add
    int slice;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void parse_matmul(Matmul_f *p);
    Merge_matmul() { name = "Merge_matmul"; }
};


class Relu_f : public comp_base {
public:
    int N;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    Relu_f() { name = "Relu_f"; }
};


class Residual_f : public comp_base {
public:
    int N;
    int inp2_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void initialize();
    Residual_f() { name = "Residual_f"; }
};


class Split_conv : public comp_base {
public:
    int W, H, C, B;
    int pX, pY, S, K;
    int slice;

    int new_H;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void parse_conv(Conv_f *p);
    Split_conv() { name = "Split_conv"; }
};


class Split_matmul : public comp_base {
public:
    int B, T, C;
    int dim;
    int slice;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    void parse_matmul(Matmul_f *matmul);
    Split_matmul() { name = "Split_matmul"; }
};

class Send_global_memory : public comp_base {
public:

    GLOBAL_SEND_TYPE type;
    int des_id; // global memory's id (reserved for c2c)
    int des_offset; // global memory's offset
    int local_offset; // local memory's offset
    int max_packet; // max packet size
    int tag_id; // tag id
    int end_length; // end length

    int data_packet_id; //已经发送的包数量

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize(); 
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    // void print_self(string prefix);
    // int sram_utilization(DATATYPE datatype);

    Send_global_memory() { name = "Send_global_memory"; }
};

class Recv_global_memory : public comp_base {
public:


    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    Recv_global_memory() { name = "Recv_global_memory"; }
};