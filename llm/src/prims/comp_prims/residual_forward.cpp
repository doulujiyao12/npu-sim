#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

REGISTER_PRIM(Residual_f)

void Residual_f::initialize() {
    auto &p = param_value;
    data_size_input = {p["N"], p["N"]};
    data_chunk = {{"output", p["N"]}};
}

void Residual_f::taskCore(TaskCoreContext &context, string prim_name,
                         u_int64_t dram_time, u_int64_t &exu_ops,
                         u_int64_t &sfu_ops) {
    auto &p = param_value;
    exu_ops = (u_int64_t)p["N"];
    sfu_ops = 0;
}