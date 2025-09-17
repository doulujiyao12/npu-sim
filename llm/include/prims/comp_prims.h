#pragma once
#include "systemc.h"

#include "common/system.h"
#include "prims/comp_base.h"

class Attention_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Attention_f() {
        name = "Attention_f";
        prim_type_code = PRIM_TYPE_CODE::ATTENTION_F_TYPE;
        param_name = {"B", "T", "C", "NH", "R"};
    }
};


class Batchnorm_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Batchnorm_f() {
        name = "Batchnorm_f";
        prim_type_code = PRIM_TYPE_CODE::BATCHNORM_F_TYPE;
        param_name = {"B", "W", "H", "C"};
    }
};


class Conv_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Conv_f() {
        name = "Conv_f";
        prim_type_code = PRIM_TYPE_CODE::CONV_F_TYPE;
        param_name = {"B",  "W",  "H",  "C",  "pX", "pY",
                      "sX", "sY", "kX", "kY", "F"};
    }
};


class Dummy_p : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Dummy_p() {
        name = "Dummy_p";
        prim_type_code = PRIM_TYPE_CODE::DUMMY_P_TYPE;
    }
};

class gate_forward : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    gate_forward() {
        name = "gate_forward";
        prim_type_code = PRIM_TYPE_CODE::GATE_FORWARD_TYPE;
        param_name = {"B", "T", "C", "E_N", "K"};
    }
};

class Gelu_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Gelu_f() {
        name = "Gelu_f";
        prim_type_code = PRIM_TYPE_CODE::GELU_F_TYPE;
        param_name = {"N"};
    }
};


class Layernorm_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Layernorm_f() {
        name = "Layernorm_f";
        prim_type_code = PRIM_TYPE_CODE::LAYERNORM_F_TYPE;
        param_name = {"B", "T", "C"};
    }
};


class Matmul_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    Matmul_f() {
        name = "Matmul_f";
        prim_type_code = PRIM_TYPE_CODE::MATMUL_F_TYPE;
        param_name = {"B", "T", "C", "OC", "NH", "DH", "R"};
    }
};


class switch_data : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    switch_data() {
        name = "switch_data";
        prim_type_code = PRIM_TYPE_CODE::SWITCH_DATA_TYPE;
        param_name = {"IN", "OUT"};
    }
};


class Max_pool : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    Max_pool() {
        name = "Max_pool";
        prim_type_code = PRIM_TYPE_CODE::MAX_POOL_TYPE;
        param_name = {"B", "W", "H", "C", "pX", "pY", "sX", "sY", "kX", "kY"};
    }
};


class Merge_conv : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Merge_conv() {
        name = "Merge_conv";
        prim_type_code = PRIM_TYPE_CODE::MERGE_CONV_TYPE;
        param_name = {"B", "T", "C", "dim", "slice"};
    }
};


class Merge_matmul : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Merge_matmul() {
        name = "Merge_matmul";
        prim_type_code = PRIM_TYPE_CODE::MERGE_MATMUL_TYPE;
        param_name = {"B", "T", "C", "dim", "slice"};
    }
};


class Relu_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    Relu_f() {
        name = "Relu_f";
        prim_type_code = PRIM_TYPE_CODE::RELU_F_TYPE;
        param_name = {"N"};
    }
};


class Residual_f : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Residual_f() {
        name = "Residual_f";
        prim_type_code = PRIM_TYPE_CODE::RESIDUAL_F_TYPE;
        param_name = {"N"};
    }
};


class rmsnorm_forward : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    rmsnorm_forward() {
        name = "rmsnorm_forward";
        prim_type_code = PRIM_TYPE_CODE::RMSNORM_F_TYPE;
        param_name = {"B", "T", "C"};
    }
};


class rope_forward : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    rope_forward() {
        name = "rope_forward";
        prim_type_code = PRIM_TYPE_CODE::ROPE_F_TYPE;
        param_name = {"B", "T", "C", "NH"};
    }
};


class silu_forward : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    silu_forward() {
        name = "silu_forward";
        prim_type_code = PRIM_TYPE_CODE::SILU_F_TYPE;
        param_name = {"N"};
    }
};


class Split_conv : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Split_conv() {
        name = "Split_conv";
        prim_type_code = PRIM_TYPE_CODE::SPLIT_CONV_TYPE;
        param_name = {"W", "H", "C", "B", "pX", "pY", "S", "K", "slice"};
    }
};


class Split_matmul : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Split_matmul() {
        name = "Split_matmul";
        prim_type_code = PRIM_TYPE_CODE::SPLIT_MATMUL_TYPE;
        param_name = {"B", "T", "C", "dim", "slice"};
    }
};


class swiglu_forward : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();
    swiglu_forward() {
        name = "swiglu_forward";
        prim_type_code = PRIM_TYPE_CODE::SWIGLU_F_TYPE;
        param_name = {"N"};
    }
};


class Send_global_memory : public CompBase {
public:
    int data_packet_id; // 已经发送的包数量

    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Send_global_memory() {
        name = "Send_global_memory";
        prim_type_code = PRIM_TYPE_CODE::SEND_GLOBAL_MEMORY_TYPE;
        param_name = {"type",         "enable",     "des_id", "des_offset",
                      "local_offset", "max_packet", "tag_id", "end_length"};
    }
};

class Recv_global_memory : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    Recv_global_memory() {
        name = "Recv_global_memory";
        prim_type_code = PRIM_TYPE_CODE::RECV_GLOBAL_MEMORY_TYPE;
        param_name = {"type", "tag_id", "recv_cnt"};
    }
};

class parse_input : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    parse_input() {
        name = "parse_input";
        prim_type_code = PRIM_TYPE_CODE::PARSE_INPUT_TYPE;
        param_name = {"size"};
    }
};

class parse_output : public CompBase {
public:
    int taskCore(TaskCoreContext &context, string prim_name,
                 u_int64_t dram_time, u_int64_t &exu_ops, u_int64_t &sfu_ops);
    void initialize();

    parse_output() {
        name = "parse_output";
        prim_type_code = PRIM_TYPE_CODE::PARSE_OUTPUT_TYPE;
        param_name = {"size"};
    }
};