#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void swiglu_forward::print_self(string prefix) {
    cout << prefix << "<swiglu_forward>\n";
    cout << prefix << "\tN: " << N << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void swiglu_forward::initialize() {
    out_size = N;
    p_inp_size = 2 * N;
    inp_size = 2 * N;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    inp2_offset = inp_offset + N * data_byte;
}

void swiglu_forward::parse_json(json j) {
    N = find_var(j["N"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int swiglu_forward::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    int inp_sram = ceiling_division(2 * N * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(N * data_byte * 8, SRAM_BITWIDTH);

    total_sram = inp_sram + out_sram;

    return total_sram;
}

void swiglu_forward::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    N = buffer.range(55, 40).to_uint64();
    datatype = DATATYPE(buffer.range(57, 56).to_uint64());

    initialize();
}

sc_bv<128> swiglu_forward::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SWIGLU_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(N);
    d.range(57, 56) = sc_bv<2>(datatype);

    return d;
}

int swiglu_forward::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_input = 2 * N;
    int data_size_input_single = N;
    int data_size_out = N;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t inp1_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t inp2_global_addr = dram_addr_tile + inp2_offset * data_byte;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;

    // 检查数据重利用
    bool input_reuse[MAX_SPLIT_NUM];
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        input_reuse[i] = false;
        if (datapass_label.indata[i][0] == '_') {
            input_reuse[i] = true;
            datapass_label.indata[i] = datapass_label.indata[i].substr(1);
        }
    }

    // 获取前缀label
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos)
        prefix = datapass_label.outdata.substr(0, pos);
    else
        prefix = datapass_label.outdata;

    // 读入input数据
    check_input_data(context, dram_time, inp1_global_addr,
                     data_size_input_single);
    check_input_data(context, dram_time, inp2_global_addr,
                     data_size_input_single);
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    // 计算过程：将input1进行silu，随后将input1与input2逐项相乘

    // 删除标签
    for (int i = 0; i < MAX_SPLIT_NUM; i++)
        if (!input_reuse[i] && datapass_label.indata[i] != UNSET_LABEL)
            sram_pos_locator->deletePair(datapass_label.indata[i]);

    BETTER_PRINT(dram_time);
#endif

    // 计算overlap并写回output数据
    write_output_data(context, 12 * N, 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int swiglu_forward::task() { return 0; }