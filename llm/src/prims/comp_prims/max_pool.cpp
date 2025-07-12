#include "systemc.h"

#include "defs/global.h"
#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Max_pool::print_self(string prefix) {
    cout << prefix << "<Maxpool_forward>\n";
    cout << prefix << "\tB: " << B << ", W: " << W << ", H: " << H
         << ", C:" << C << endl;
    cout << prefix << "\tpX: " << pX << ", pY: " << pY << ", sX: " << sX
         << ", sY:" << sY << endl;
    cout << prefix << "\toW: " << oW << ", oH: " << oH << ", oC: " << oC
         << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Max_pool::initialize() {
    oH = (H + 2 * pY - kY) / sY + 1;
    oW = (W + 2 * pX - kX) / sX + 1;
    oC = C;

    out_size = B * oC * oH * oW;
    p_inp_size = B * C * H * W;
    inp_size = B * C * H * W;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    k_offset = B * C * H * W + inp_offset;
    b_offset = 1 * C * kX * kY + k_offset;
}

void Max_pool::parse_json(json j) {
    B = find_var(j["B"]);
    W = find_var(j["W"]);
    H = find_var(j["H"]);
    C = find_var(j["C"]);
    pX = find_var(j["pX"]);
    pY = find_var(j["pY"]);
    sX = find_var(j["sX"]);
    sY = find_var(j["sY"]);
    kX = find_var(j["kX"]);
    kY = find_var(j["kY"]);


    initialize();

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}


int Max_pool::sram_utilization(DATATYPE datatype, int cid) {

    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * C * H * W * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(out_size * data_byte * 8, get_sram_bitwidth(cid));


    total_sram = p_inp_sram + out_sram;

    return total_sram;
}

void Max_pool::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset = inp_offset * 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset = out_offset * 1024;
    W = buffer.range(55, 40).to_uint64();
    H = buffer.range(71, 56).to_uint64();
    C = buffer.range(83, 72).to_uint64();
    pX = buffer.range(87, 84).to_uint64();
    pY = buffer.range(91, 88).to_uint64();
    sX = buffer.range(95, 92).to_uint64();
    sY = buffer.range(99, 96).to_uint64();
    kX = buffer.range(103, 100).to_uint64();
    kY = buffer.range(107, 104).to_uint64();

    B = buffer.range(123, 120).to_uint64();
    datatype = DATATYPE(buffer.range(125, 124).to_uint64());

    initialize();
}

sc_bv<128> Max_pool::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MAX_POOL_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(W);
    d.range(71, 56) = sc_bv<16>(H);
    d.range(83, 72) = sc_bv<12>(C);
    d.range(87, 84) = sc_bv<4>(pX);
    d.range(91, 88) = sc_bv<4>(pY);
    d.range(95, 92) = sc_bv<4>(sX);
    d.range(99, 96) = sc_bv<4>(sY);
    d.range(103, 100) = sc_bv<4>(kX);
    d.range(107, 104) = sc_bv<4>(kY);

    d.range(123, 120) = sc_bv<4>(B);
    d.range(125, 124) = sc_bv<2>(datatype);

    return d;
}

int Max_pool::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * C * H * W};
    int data_size_out = B * oC * oH * oW;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
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
    { // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    write_output_data(context, 0, B * oC * oH * oW * kX * kY, dram_time,
                      overlap_time, data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Max_pool::task() {
    //     // CTODO: 计算cycle数
    //     // TODO DAHU TIME ??


    // #if DUMMY == 1


    // #else
    //     assert(false && "Unsupported DUMMY == 0");

    // #endif
    return 0;
}
