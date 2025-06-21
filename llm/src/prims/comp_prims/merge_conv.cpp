#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"

void Merge_conv::print_self(string prefix) {}

void Merge_conv::parse_json(json j) {
    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}


void Merge_conv::deserialize(sc_bv<128> buffer) {}

void Merge_conv::parse_matmul(Matmul_f *p) {}

sc_bv<128> Merge_conv::serialize() {
    sc_bv<128> d(0x1);
    return d;
}
int Merge_conv::task_core(TaskCoreContext &context) { return 0; }
int Merge_conv::task() { return 1; }

int Merge_conv::sram_utilization(DATATYPE datatype, int cid) { return 0; }