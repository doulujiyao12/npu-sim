#include "prims/moe_base.h"

class matmul_forward_moe : public moe_base {
public: 
    int B, T, C, OC;
    int K; // 选取的专家个数
    int E_N; // 专家个数

    int w_offset, b_offset;
    bool is_merge; // true: FFN升维，否则为降维

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parseJson(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);

    void initialize();
    HardwareTaskConfig *generate_hw_config();
    matmul_forward_moe() { name = "matmul_forward_moe"; }
};


class load_expert : public moe_base {
public:
    int E_N; // 专家个数
    int K; // 需要选择的专家个数
    int OC, C;
    MOE_LOAD_STRATEGY strategy;

    int task();
    int taskCoreDefault(TaskCoreContext &context);

    sc_bv<128> serialize();
    void deserialize(sc_bv<128> buffer);

    void parseJson(json j);
    void print_self(string prefix);
    int sram_utilization(DATATYPE datatype, int cid = 0);

    void initialize();
    load_expert() { name = "load_expert"; }
};