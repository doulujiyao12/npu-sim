#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"

REGISTER_PRIM(Dummy_p)

void Dummy_p::initialize() { data_size_input = {80}; }

void Dummy_p::taskCore(TaskCoreContext &context, string prim_name,
                      u_int64_t dram_time, u_int64_t &exu_ops,
                      u_int64_t &sfu_ops) {
    exu_ops = 10;
}