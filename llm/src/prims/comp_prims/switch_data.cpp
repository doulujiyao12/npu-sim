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

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void switch_data::parse_json(json j) {
    IN = find_var(j["IN"]);
    OUT = find_var(j["OUT"]);

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);

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
    d.range(7, 0) = sc_bv<8>(SWITCH_DATA_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(IN);
    d.range(71, 56) = sc_bv<16>(OUT);
    d.range(73, 72) = sc_bv<2>(datatype);

    return d;
}

int switch_data::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_input = IN;
    int data_size_out = OUT;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
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
    write_output_data(context, 0, 0, dram_time, overlap_time, data_size_out,
                      out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int switch_data::task() { return 0; }