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