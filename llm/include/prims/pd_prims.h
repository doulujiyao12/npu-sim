#include "prims/pd_base.h"

class matmul_forward_pd : public pd_base {
public:
    int B, T, C, OC;
    int w_offset, b_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void initialize();

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    matmul_forward_pd() { name = "matmul_forward_pd"; }
};


class Matmul_f_decode : public pd_base {
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


class Matmul_f_prefill : public pd_base {
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



class attention_forward_pd : public pd_base {
public:
    int B, T, C, NH;
    int prea_offset, a_offset;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void initialize();

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);

    attention_forward_pd() { name = "attention_forward_pd"; }
};

class Attention_f_decode : public pd_base {
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

class Attention_f_prefill : public pd_base {
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


    Attention_f_prefill() { name = "Attention_f_prefill"; }
};

class rope_f : public pd_base {
public:
    int B, T, C, NH;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype);
    void initialize();

    rope_f() { name = "rope_f"; }
};