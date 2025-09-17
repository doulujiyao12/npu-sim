#include "systemc.h"
#include <tlm>

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

void Recv_global_memory::initialize() {
    ARGUS_EXIT("Recv_global_memory not implemented.\n");
}

int Recv_global_memory::taskCore(TaskCoreContext &context, string prim_name,
                                 u_int64_t dram_time, u_int64_t &exu_ops,
                                 u_int64_t &sfu_ops) {
    ARGUS_EXIT("Recv_global_memory not implemented.\n");
}