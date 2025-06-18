#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Conv_f::print_self(string prefix) {
    cout << prefix << "<convolution_forward>\n";
    cout << prefix << "\tB: " << B << ", W: " << W << ", H: " << H
         << ", C:" << C << endl;
    cout << prefix << "\tpX: " << pX << ", pY: " << pY << ", sX: " << sX
         << ", sY:" << sY << endl;
    cout << prefix << "\tkX: " << kX << ", kY: " << kY << ", F: " << F << endl;
    cout << prefix << "\toW: " << oW << ", oH: " << oH << ", oC: " << oC
         << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Conv_f::initialize() {
    oH = (H + 2 * pY - kY) / sY + 1;
    oW = (W + 2 * pX - kX) / sX + 1;
    oC = F;

    out_size = B * oC * oH * oW;
    p_inp_size = B * C * H * W;
    inp_size = B * C * H * W + F * C * kX * kY + F;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void Conv_f::parse_json(json j) {
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
    F = find_var(j["F"]);
    std::cout << "Variables_p:" << std::endl;
    std::cout << "B (Batch size): " << B << std::endl;
    std::cout << "C (Input Channels): " << C << std::endl;
    std::cout << "oC (Output Channels): " << F << std::endl;
    std::cout << "H (Output Height): " << H << std::endl;
    std::cout << "W (Output Width): " << W << std::endl;
    std::cout << "kX (Kernel Width): " << kX << std::endl;
    std::cout << "kY (Kernel Height): " << kY << std::endl;

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}
int Conv_f::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    int p_inp_sram_byte = B * C * H * W * data_byte * 8;
    int p_inp_sram = ceiling_division(p_inp_sram_byte, get_sram_bitwidth(cid));
    int w1_inps_sram = ceiling_division(F * C * kX * kY * data_byte * 8,
                                        get_sram_bitwidth(cid));
    int b_sram = ceiling_division(F * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(out_size * data_byte * 8, get_sram_bitwidth(cid));


    total_sram = p_inp_sram + w1_inps_sram + b_sram + out_sram;

    // std::cout << "Variables1:" << std::endl;
    // std::cout << "B (Batch size): " << B << std::endl;
    // std::cout << "C (Input Channels): " << C << std::endl;
    // std::cout << "oC (Output Channels): " << F << std::endl;
    // std::cout << "H (Output Height): " << H << std::endl;
    // std::cout << "W (Output Width): " << W << std::endl;
    // std::cout << "kX (Kernel Width): " << kX << std::endl;
    // std::cout << "kY (Kernel Height): " << kY << std::endl;

    // std::cout << RED << "p_inp_sram = " << p_inp_sram << RESET << std::endl;
    // std::cout << RED << "w1_inps_sram = " << w1_inps_sram << RESET <<
    // std::endl; std::cout << RED << "b_sram = " << b_sram << RESET <<
    // std::endl; std::cout << RED << "out_sram = " << out_sram << RESET <<
    // std::endl; std::cout << RED << "total_sram = " << total_sram << RESET <<
    // std::endl;

    return total_sram;
}

void Conv_f::deserialize(sc_bv<128> buffer) {
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
    F = buffer.range(119, 108).to_uint64();
    B = buffer.range(123, 120).to_uint64();
    datatype = DATATYPE(buffer.range(125, 124).to_uint64());
    // std::cout << "Variables1:" << std::endl;
    // std::cout << "B (Batch size): " << B << std::endl;
    // std::cout << "C (Input Channels): " << C << std::endl;
    // std::cout << "oC (Output Channels): " << F << std::endl;
    // std::cout << "H (Output Height): " << H << std::endl;
    // std::cout << "W (Output Width): " << W << std::endl;
    // std::cout << "kX (Kernel Width): " << kX << std::endl;
    // std::cout << "kY (Kernel Height): " << kY << std::endl;

    initialize();

    k_offset = B * C * H * W + inp_offset;
    b_offset = F * C * kX * kY + k_offset;
}

sc_bv<128> Conv_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(CONV_F_TYPE);
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
    d.range(119, 108) = sc_bv<12>(F);
    d.range(123, 120) = sc_bv<4>(B);
    d.range(125, 124) = sc_bv<2>(datatype);
    // std::cout << "Variables2:" << std::endl;
    // std::cout << "B (Batch size): " << B << std::endl;
    // std::cout << "C (Input Channels): " << C << std::endl;
    // std::cout << "oC (Output Channels): " << F << std::endl;
    // std::cout << "H (Output Height): " << H << std::endl;
    // std::cout << "W (Output Width): " << W << std::endl;
    // std::cout << "kX (Kernel Width): " << kX << std::endl;
    // std::cout << "kY (Kernel Height): " << kY << std::endl;

    return d;
}
int Conv_f::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_input = B * C * H * W;
    int data_size_weight = F * C * kX * kY;
    int data_size_bias = F;
    int data_size_out = B * oC * oH * oW;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t weight_global_addr = dram_addr_tile + k_offset * data_byte;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;
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
    printf("rope_f: dram time 1: %ld\n", dram_time);

#if USE_SRAM == 1
    auto label_weight = ETERNAL_PREFIX + prefix + "_w";
    check_static_data(context, dram_time, weight_global_addr, data_size_weight,
                      label_weight);

    auto label_bias = ETERNAL_PREFIX + prefix + "_b";
    check_static_data(context, dram_time, bias_global_addr, data_size_bias,
                      label_bias);

    printf("Conv_f: dram time 1: %ld\n", dram_time);

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    printf("Conv_f: dram time 2: %ld\n", dram_time);
#else
    assert(false && "Unsupported USE_SRAM == 0");
#endif

    write_output_data(context, B * C * oC * oH * oW * kX * kY * 2, 0, dram_time,
                      overlap_time, data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Conv_f::task() {
    //     // CTODO: 计算cycle数
    //     // TODO DAHU TIME ??


    // #if DUMMY == 1


    // #else
    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *input = dram_start + inp_offset;
    //     float *output = dram_start + out_offset;
    //     float *kernel = dram_start + k_offset;
    //     float *bias = dram_start + b_offset;
    //     std::vector<float> padded_input(B * C * (H + 2 * pY) * (W + 2 * pX),
    //     0);

    //     for (int b = 0; b < B; ++b) {
    //         for (int c = 0; c < C; ++c) {
    //             for (int h = 0; h < H; ++h) {
    //                 for (int w = 0; w < W; ++w) {
    //                     padded_input[b * C * (H + 2 * pY) * (W + 2 * pX) +
    //                                  c * (H + 2 * pY) * (W + 2 * pX) +
    //                                  (h + pY) * (W + 2 * pX) + (w + pX)] =
    //                         input[b * C * H * W + c * H * W + h * W + w];
    //                 }
    //             }
    //         }
    //     }

    //     // 进行卷积操作
    //     for (int b = 0; b < B; ++b) {
    //         for (int c_out = 0; c_out < oC; ++c_out) {
    //             for (int h_out = 0; h_out < oH; ++h_out) {
    //                 for (int w_out = 0; w_out < oW; ++w_out) {
    //                     float sum = 0.0f;

    //                     for (int c_in = 0; c_in < C; ++c_in) {
    //                         for (int k_h = 0; k_h < kY; ++k_h) {
    //                             for (int k_w = 0; k_w < kX; ++k_w) {
    //                                 int h_in = h_out * sY + k_h;
    //                                 int w_in = w_out * sX + k_w;

    //                                 sum +=
    //                                     padded_input[b * C * (H + 2 * pY) *
    //                                                      (W + 2 * pX) +
    //                                                  c_in * (H + 2 * pY) *
    //                                                      (W + 2 * pX) +
    //                                                  h_in * (W + 2 * pX) +
    //                                                  w_in] *
    //                                     kernel[c_out * C * kY * kX +
    //                                            c_in * kY * kX + k_h * kX +
    //                                            k_w];
    //                             }
    //                         }
    //                     }

    //                     output[b * oC * oH * oW + c_out * oH * oW + h_out *
    //                     oW +
    //                            w_out] = sum + bias[c_out];
    //                 }
    //             }
    //         }
    //     }
    // #endif
    return 0;
}
