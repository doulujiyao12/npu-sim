#include "prims/comp_prims.h"
#include "utils/memory_utils.h"

int rope_forward::task() { return 0; }

int rope_forward::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_sincos = B * (C / NH) * 2;
    int data_size_out = B * T * C; // QKV

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t sincos_global_addr = inp_global_addr + sc_offset * data_byte;
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
    // 此时默认已经分好注意力头了。对于每一个注意力头，对应的sincos数据大小均为B
    // * T * (C / NH) (最后一个维度已扩展)
    // 读出需要用到的sincos数据
    int max_token_num = T;
    data_size_sincos *= max_token_num;

    // 读入sincos数据
    auto label_sincos = ETERNAL_PREFIX + prefix + "_sc";
    check_static_data(context, dram_time, sincos_global_addr, data_size_sincos,
                      label_sincos);

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    BETTER_PRINT(dram_time);
#endif

    // 计算overlap并写回output数据
    write_output_data(context, 6 * T * C, 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

sc_bv<128> rope_forward::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(ROPE_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    return d;
}

void rope_forward::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint();
    out_offset = buffer.range(39, 24).to_uint();
    B = buffer.range(55, 40).to_uint();
    T = buffer.range(71, 56).to_uint();
    C = buffer.range(87, 72).to_uint();
    NH = buffer.range(103, 88).to_uint();

    initialize();
}

void rope_forward::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    NH = find_var(j["NH"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

void rope_forward::print_self(string prefix) {
    cout << prefix << "rope_forward: B=" << B << ", T=" << T << ", C=" << C
         << endl;
}

int rope_forward::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void rope_forward::initialize() {
    inp_size = B * T * C;
    p_inp_size = inp_size;
    out_size = B * T * C * 1 / 3;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    sc_offset = inp_offset + B * T * C;
}