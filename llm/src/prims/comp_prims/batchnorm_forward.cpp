#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/system_utils.h"

void Batchnorm_f::initialize() {}

int Batchnorm_f::taskCore(TaskCoreContext &context, string prim_name,
                          u_int64_t dram_time, u_int64_t &exu_ops,
                          u_int64_t &sfu_ops) {
    exu_ops = 0;
    sfu_ops = 0;
}