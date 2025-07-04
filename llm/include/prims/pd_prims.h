#include "prims/pd_base.h"

class matmul_forward_pd : public pd_base {
public:
    int B, T, C, OC;
    int NH, DH, R;
    int w_offset, b_offset;
    PD_JOB job_type;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void initialize();

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);

    matmul_forward_pd() { name = "matmul_forward_pd"; }
};


class attention_forward_pd : public pd_base {
public:
    int B, T, C, NH;
    int DH, R;
    int prea_offset, a_offset;
    PD_JOB job_type;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void initialize();

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);

    attention_forward_pd() { name = "attention_forward_pd"; }
};


class rope_forward_pd : public pd_base {
public:
    int B, T, C, NH;
    int R;
    int sc_offset;
    PD_JOB job_type;

    int task();
    int task_core(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parse_json(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);
    void initialize();

    rope_forward_pd() { name = "rope_forward_pd"; }
};