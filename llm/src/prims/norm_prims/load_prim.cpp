#include "systemc.h"

#include "prims/norm_prims.h"
#include "prims/prim_base.h"

void Load_prim::print_self(string prefix) {}

void Load_prim::parse_json(json j, vector<pair<string, int>> vtable) {}

int Load_prim::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    return total_sram;
}

void Load_prim::deserialize(sc_bv<128> buffer) {
    dram_addr = buffer.range(23, 8).to_uint64();
    sram_addr = buffer.range(39, 24).to_uint64();
    size = buffer.range(55, 40).to_uint64();
    datatype = (DATATYPE)buffer.range(57, 56).to_uint64();
}

sc_bv<128> Load_prim::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(LOAD_PRIM_TYPE);
    d.range(23, 8) = sc_bv<16>(dram_addr);
    d.range(39, 24) = sc_bv<16>(sram_addr);
    d.range(55, 40) = sc_bv<16>(size);
    d.range(57, 56) = sc_bv<2>(datatype);


    return d;
}
int Load_prim::task_core(TaskCoreContext &context) { return 0; }
int Load_prim::task() {
    // CTODO: complete this after dram and sram interface are done
    return 0;
}