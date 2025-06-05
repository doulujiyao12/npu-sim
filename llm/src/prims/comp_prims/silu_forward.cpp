#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void silu_forward::print_self(string prefix) {
    cout << prefix << "<silu_forward>\n";
    cout << prefix << "\tN: " << N << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void silu_forward::initialize() {
    out_size = N;
    p_inp_size = N;
    inp_size = N;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void silu_forward::parse_json(json j) {
    N = find_var(j["N"]);

    initialize();

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

int silu_forward::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;
    int data_byte = 0;

    if (datatype == DATATYPE::FP16) {
        data_byte = 2;
    } else if (datatype == DATATYPE::INT8) {
        data_byte = 1;
    }

    int inp_sram = ceiling_division(N * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(N * data_byte * 8, SRAM_BITWIDTH);

    total_sram = inp_sram + out_sram;

    return total_sram;
}

void silu_forward::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    N = buffer.range(55, 40).to_uint64();
    datatype = DATATYPE(buffer.range(57, 56).to_uint64());

    initialize();
}

sc_bv<128> silu_forward::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SILU_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(N);
    d.range(57, 56) = sc_bv<2>(datatype);

    return d;
}

int silu_forward::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_input = N;
    int data_size_out = N;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;

    // 检查数据重利用
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    // 获取前缀label
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos)
        prefix = datapass_label.outdata.substr(0, pos);
    else
        prefix = datapass_label.outdata;

    // 读入input数据
    check_input_data(context, dram_time, inp_global_addr, data_size_input);
    printf("silu_f: dram time 1: %ld\n", dram_time);

#if USE_SRAM == 1
    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    printf("rope_f: dram time 2: %ld\n", dram_time);
#endif

    // 计算overlap并写回output数据
    write_output_data(context, 8 * N, dram_time, overlap_time,
                      data_size_out);
    printf("rope_f: overlap_time: %ld\n", overlap_time);

    return overlap_time;
}

int silu_forward::task() { return 0; }