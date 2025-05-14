#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void switch_data::print_self(string prefix) {
    cout << prefix << "<switch_data>\n";
    cout << prefix << "\tIN: " << IN << ", OUT: " << OUT << endl;
}

void switch_data::initialize() {
    out_size = OUT;
    inp_size = IN;
    p_inp_size = IN;
}

void switch_data::parse_json(json j) {
    IN = find_var(j["IN"]);
    OUT = find_var(j["OUT"]);

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }

    initialize();
}

int switch_data::sram_utilization(DATATYPE datatype) { return 0; }

void switch_data::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    IN = buffer.range(55, 40).to_uint64();
    OUT = buffer.range(71, 56).to_uint64();
    datatype = DATATYPE(buffer.range(73, 72).to_uint64());

    initialize();
}

sc_bv<128> switch_data::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xd4);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(IN);
    d.range(71, 56) = sc_bv<16>(OUT);
    d.range(73, 72) = sc_bv<2>(datatype);

    return d;
}

int switch_data::task_core(TaskCoreContext &context) {
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

    u_int64_t dram_time = 0;


    int data_size_input = IN;
    int data_size_out = OUT;

#if USE_SRAM == 1
    // 检查是否可以在此原语结束之后立刻释放中间结果
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    auto inp_sram_offset = 0;
    if (datapass_label.indata[0].find(DRAM_LABEL) == 0) {
        sram_first_write_generic(context, data_byte * data_size_input,
                                 inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] =
                datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] core %d, Layernorm_f: read from dram, label: %s\n", cid,
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                              inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] core %d, Layernorm_f: sram_pos_locator cannot find "
                   "the "
                   "label: %s\n",
                   cid, datapass_label.indata[0].c_str());
            sc_stop();
        } else if (flag > 0) {
            sram_first_write_generic(context, flag, inp_global_addr, dram_time,
                                     dram_start);
            inp_key.size = data_byte * data_size_input;
            inp_key.spill_size = 0;
            sram_pos_locator->addPair(datapass_label.indata[0], inp_key,
                                      context, dram_time);
        }
    }
#else
#endif

    u_int64_t cycle = 0;
#if USE_SRAM == 1
    // 写入out
    // label kv in sram locator
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context, cycle);
    sram_write_append_generic(context, data_byte * data_size_out, cycle);
#else
    // CTODO: do dram only
#endif
    printf("core %d, layernorm_forward: overlap_time: %ld\n", cid, cycle);
    return cycle;
}

int switch_data::task() { return 0; }