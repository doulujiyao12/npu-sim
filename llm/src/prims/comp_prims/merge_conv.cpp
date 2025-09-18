#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/base.h"
#include "prims/comp_prims.h"

void Merge_conv::initialize() {}

int Merge_conv::taskCore(TaskCoreContext &context, string prim_name,
                         u_int64_t dram_time, u_int64_t &exu_ops,
                         u_int64_t &sfu_ops) {}