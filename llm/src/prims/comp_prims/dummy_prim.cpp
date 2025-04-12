#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"

void Dummy_p::print_self(string prefix) {
    cout << prefix << "<dummy_prim>\n";
    cout << prefix << "\t;)\n";
}

void Dummy_p::parse_json(json j) {
    out_size = 80;
    inp_size = 80;
    p_inp_size = 80;

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}
int Dummy_p::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    if (datatype == DATATYPE::FP16) {
        total_sram = 2 * (out_size + inp_size);
    } else if (datatype == DATATYPE::INT8) {
        total_sram = out_size + inp_size;
    }

    return total_sram;
}

void Dummy_p::deserialize(sc_bv<128> buffer) {}

sc_bv<128> Dummy_p::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xd0);
    d.range(9, 8) = sc_bv<2>(datatype);

    return d;
}

int Dummy_p::task_core(TaskCoreContext &context) { return 0; }
int Dummy_p::task() {
    cout << sc_time_stamp() << ": Worker " << cid << " executing dummy.\n";
    return 15;
}