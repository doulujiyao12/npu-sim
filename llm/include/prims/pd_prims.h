#include "prims/base.h"

class matmul_forward_pd : public PdBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    matmul_forward_pd() {
        name = "matmul_forward_pd";
        param_name.insert(param_name.end(),
                          {"B", "T", "C", "OC", "NH", "DH", "R", "chunk"});
    }
};


class attention_forward_pd : public PdBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    attention_forward_pd() {
        name = "attention_forward_pd";
        param_name.insert(param_name.end(), {"B", "T", "C", "NH", "DH", "R"});
    }
};


class rope_forward_pd : public PdBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    rope_forward_pd() {
        name = "rope_forward_pd";
        param_name.insert(param_name.end(), {"B", "T", "C", "NH", "R"});
    }
};