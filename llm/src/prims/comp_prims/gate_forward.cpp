#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void gate_forward::print_self(string prefix) {
    cout << prefix << "gate_forward:" << endl;
    cout << prefix << "  B: " << B << endl;
    cout << prefix << "  T: " << T << endl;
    cout << prefix << "  C: " << C << endl;
    cout << prefix << "  E_N: " << E_N << endl;
    cout << prefix << "  K: " << K << endl;
}

void gate_forward::initialize() {
    out_size = B * T * K;
    p_inp_size = B * T * C;
    inp_size = B * T * C;

    dram_inp_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (B * T * K + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = 0;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void gate_forward::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    E_N = find_var(j["E_N"]);
    K = find_var(j["K"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);

    if (inp_offset == -1)
        inp_offset = (out_offset * 1024 - B * T * C) / 1024;

    cout << "\033[1;33m" << "gate_forward" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
}

int gate_forward::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * T * C * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(B * T * K * data_byte * 8, get_sram_bitwidth(cid));

    total_sram = p_inp_sram + out_sram;

    return total_sram;
}

void gate_forward::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint();
    out_offset *= 1024;

    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    E_N = buffer.range(103, 88).to_uint64();
    K = buffer.range(119, 104).to_uint64();
    datatype = DATATYPE(buffer.range(121, 120).to_uint64());

    initialize();
}

sc_bv<128> gate_forward::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(GATE_FORWARD_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(E_N);
    d.range(119, 104) = sc_bv<16>(K);
    d.range(121, 120) = sc_bv<2>(datatype);

    return d;
}

int gate_forward::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_out = B * T * K;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;

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
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    {
        // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    write_output_data(context, B * T * C * E_N, 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);

    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int gate_forward::task() { return 0; }