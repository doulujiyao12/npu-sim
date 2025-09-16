#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

void rmsnorm_forward::print_self(string prefix) {
    cout << prefix << "<rmsnorm_forward>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << input_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void rmsnorm_forward::initialize() {
    out_size = B * T * C;
    input_size = B * T * C;
    inp_size = B * T * C + C;

    dram_inp_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = (C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    w_offset = B * T * C + inp_offset;
}

void rmsnorm_forward::parseJson(json j) {
    B = GetDefinedParam(j["B"]);
    T = GetDefinedParam(j["T"]);
    C = GetDefinedParam(j["C"]);

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (inp_offset == -1 && out_offset == -1 && data_offset == -1)
        assert(0 && "no dram address found");

    if (inp_offset == -1 && data_offset != -1)
        inp_offset = (data_offset * 1024 - B * T * C) / 1024;

    if (out_offset == -1 && data_offset != -1)
        out_offset = (data_offset * 1024 + C + C) / 1024;

    // 添加以下三行以打印相关信息
    cout << "\033[1;33m" << "Layernorm_f" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
    cout << "data_offset: " << data_offset << endl;

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);

    initialize();
}

int rmsnorm_forward::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    int p_inp_sram =
        CeilingDivision(B * T * C * data_byte * 8, GetCoreHWConfig(cid).sram_bitwidth);
    int w_sram = CeilingDivision(C * data_byte * 8, GetCoreHWConfig(cid).sram_bitwidth);
    int out_sram =
        CeilingDivision(out_size * data_byte * 8, GetCoreHWConfig(cid).sram_bitwidth);

    total_sram = p_inp_sram + w_sram + out_sram;
    total_sram *= GetCoreHWConfig(cid).sram_bitwidth / 8;

    return total_sram;
}

void rmsnorm_forward::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    datatype = DATATYPE(buffer.range(89, 88).to_uint64());

    initialize();
}

sc_bv<128> rmsnorm_forward::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(RMSNORM_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(89, 88) = sc_bv<2>(datatype);

    return d;
}

int rmsnorm_forward::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_weight = C;
    int data_size_out = B * T * C;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t weight_global_addr = dram_addr_tile + w_offset * data_byte;
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
    checkInputData(context, dram_time, inp_global_addr, data_size_input);
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    // 读入weight数据
    auto label_weight = ETERNAL_PREFIX + prefix + "_w";
    checkStaticData(context, dram_time, weight_global_addr, data_size_weight,
                      label_weight);

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    BETTER_PRINT(dram_time);
#endif

    // 计算overlap并写回output数据
    writeOutputData(context, B * T * (4 * C + 3), 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int rmsnorm_forward::task() { return 0; }