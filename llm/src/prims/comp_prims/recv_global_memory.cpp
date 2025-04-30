#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

sc_bv<128> Recv_global_memory::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x21);
    assert(false && "Recv_global_memory is not implemented");

}

void Recv_global_memory::deserialize(sc_bv<128> buffer) {

}

int Recv_global_memory::task() {
    assert(false && "Recv_global_memory is not implemented");
    return 0;
}

int Recv_global_memory::task_core(TaskCoreContext &context) {
    return 0;
}