#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"

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

int Dummy_p::task_core(TaskCoreContext &context) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;
#endif

    // TODO vector load
    // DAHU dram_time 不对
    u_int64_t dram_time = 0;

    int data_size_input = 80;
    int data_size_out = 80;
    u_int64_t overlap_time = 0;

#if USE_SRAM == 1
    // 写入out
    // label kv in sram locator
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#endif
    return 10;
}

int Dummy_p::task() {
    cout << sc_time_stamp() << ": Worker " << cid << " executing dummy.\n";
    return 15;
}