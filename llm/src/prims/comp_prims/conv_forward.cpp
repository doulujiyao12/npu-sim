#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Conv_f::print_self(string prefix) {
    cout << prefix << "<convolution_forward>\n";
    cout << prefix << "\tB: " << B << ", W: " << W << ", H: " << H << ", C:" << C << endl;
    cout << prefix << "\tpX: " << pX << ", pY: " << pY << ", sX: " << sX << ", sY:" << sY << endl;
    cout << prefix << "\tkX: " << kX << ", kY: " << kY << ", F: " << F << endl;
    cout << prefix << "\toW: " << oW << ", oH: " << oH << ", oC: " << oC << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset << ", input_offset: " << inp_offset << endl;
}

void Conv_f::initialize() {
    oH = (H + 2 * pY - kY) / sY + 1;
    oW = (W + 2 * pX - kX) / sX + 1;
    oC = F;

    out_size = B * oC * oH * oW;
    p_inp_size = B * C * H * W;
    inp_size = B * C * H * W + F * C * kX * kY + F;

    out_dim.push_back(B);
    out_dim.push_back(oC);
    out_dim.push_back(oH);
    out_dim.push_back(oW);
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

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}
int Conv_f::sram_utilization(DATATYPE datatype) {

    int total_sram = 0;
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    int p_inp_sram_byte = B * C * H * W * data_byte * 8;
    int p_inp_sram = ceiling_division(p_inp_sram_byte, (int)SRAM_BITWIDTH);
    int w1_inps_sram = ceiling_division(F * C * kX * kY * data_byte * 8, (int)SRAM_BITWIDTH);
    int b_sram = ceiling_division(F * data_byte * 8, (int)SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, (int)SRAM_BITWIDTH);


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
    d.range(7, 0) = sc_bv<8>(0xa);
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
    u_int64_t weight_global_addr = dram_addr_tile + k_offset * data_byte;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *input = dram_start + inp_offset;
    float *output = dram_start + out_offset;
    float *kernel = dram_start + k_offset;
    float *bias = dram_start + b_offset;
#endif
    u_int64_t dram_time = 0;


    int data_size_input = B * C * H * W;
    int data_size_weight = F * C * kX * kY;
    int data_size_bias = F;
    int data_size_out = B * oC * oH * oW;

#if USE_SRAM == 1
    // 检查是否可以在此原语结束之后立刻释放中间结果
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    auto inp_sram_offset = 0;
    if (datapass_label.indata[0].find(DRAM_LABEL) == 0) {
        sram_first_write_generic(context, data_byte * data_size_input, inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] = datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] Conv_f: read from dram, label: %s\n", datapass_label.indata[0].c_str());

        SramPosKey inp_key = SramPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context, dram_time);
    } else {
        SramPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0], inp_key);
        if (flag == -1) {
            printf("[ERROR] Conv_f: sram_pos_locator cannot find the label: %s\n", datapass_label.indata[0].c_str());
            sc_stop();
        } else if (flag > 0) {
            sram_first_write_generic(context, flag, inp_global_addr, dram_time, dram_start);
            inp_key.size = data_byte * data_size_input;
            inp_key.spill_size = 0;
            sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context, dram_time);
        }
    }

    // 获取前缀label
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos) {
        prefix = datapass_label.outdata.substr(0, pos);
    } else {
        prefix = datapass_label.outdata;
    }

    auto label_weight = ETERNAL_PREFIX + prefix + "_w";
    SramPosKey w_key;
    int flag = sram_pos_locator->findPair(label_weight, w_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_weight, weight_global_addr, dram_time, dram_start);

        w_key = SramPosKey(*sram_addr, data_byte * data_size_weight);
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    } else if (flag > 0) {
        sram_first_write_generic(context, flag, inp_global_addr, dram_time, dram_start);
        w_key.size = data_byte * data_size_weight;
        w_key.spill_size = 0;
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    }

    auto label_bias = ETERNAL_PREFIX + prefix + "_b";
    SramPosKey b_key;
    flag = sram_pos_locator->findPair(label_bias, b_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_bias, bias_global_addr, dram_time, dram_start);

        b_key = SramPosKey(*sram_addr, data_byte * data_size_bias);
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    } else if (flag > 0) {
        sram_first_write_generic(context, flag, bias_global_addr, dram_time, dram_start);
        b_key.size = data_byte * data_size_bias;
        b_key.spill_size = 0;
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    }

    printf("Conv_f: dram time 1: %ld\n", dram_time);

    // 简化读出所有数据即可
    int w_sram_offset, b_sram_offset;
    sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
    sram_pos_locator->findPair(label_weight, w_sram_offset);
    sram_pos_locator->findPair(label_bias, b_sram_offset);
    sram_read_generic(context, data_byte * data_size_input, inp_sram_offset, dram_time);
    sram_read_generic(context, data_byte * data_size_weight, w_sram_offset, dram_time);
    sram_read_generic(context, data_byte * data_size_bias, b_sram_offset, dram_time);

    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("Conv_f: dram time 2: %ld\n", dram_time);
#else
    assert(false && "Unsupported USE_SRAM == 0");
#endif


    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * C * oC * oH * oW * kX * kY * 2 / (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
        cout << "Conv_f1: dram time: " << dram_time << " cycle: " << cycle << endl;
        std::cout << "Variables:" << std::endl;
        std::cout << "B (Batch size): " << B << std::endl;
        std::cout << "C (Input Channels): " << C << std::endl;
        std::cout << "oC (Output Channels): " << oC << std::endl;
        std::cout << "oH (Output Height): " << oH << std::endl;
        std::cout << "oW (Output Width): " << oW << std::endl;
        std::cout << "kX (Kernel Width): " << kX << std::endl;
        std::cout << "kY (Kernel Height): " << kY << std::endl;
        std::cout << "tile_exu.x_dims: " << tile_exu.x_dims << std::endl;
        std::cout << "tile_exu.y_dims: " << tile_exu.y_dims << std::endl;
        std::cout << "CYCLE: " << CYCLE << std::endl;

        // 打印计算结果
        std::cout << "Calculated cycle: " << cycle << std::endl;
    } else {
        assert(false && "Unsupported tile type");
    }
    u_int64_t overlap_time = 0;

#if USE_SRAM == 1
    cout << "Conv_f: dram time: " << dram_time << " cycle: " << cycle << endl;
    if (dram_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time << RESET << std::endl;
    } else {
        overlap_time = cycle - dram_time;
        std::cout << GREEN << "cycle: " << cycle << ", dram_time: " << dram_time << RESET << std::endl;
    }
#else
    if (dram_time > cycle) {
        overlap_time = dram_time;
    } else {
        overlap_time = cycle;
    }
#endif

#if USE_SRAM == 1
    // 写入out
    // label kv in sram locator
    SramPosKey out_key = SramPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context, dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#else
    assert(false && "Unsupported USE_SRAM == 0");
#endif
    printf("output conv_f_cycle %ld \n", overlap_time);
    return overlap_time;
}
int Conv_f::task() {
    // CTODO: 计算cycle数
    // TODO DAHU TIME ??


#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *input = dram_start + inp_offset;
    float *output = dram_start + out_offset;
    float *kernel = dram_start + k_offset;
    float *bias = dram_start + b_offset;
    std::vector<float> padded_input(B * C * (H + 2 * pY) * (W + 2 * pX), 0);

    for (int b = 0; b < B; ++b) {
        for (int c = 0; c < C; ++c) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    padded_input[b * C * (H + 2 * pY) * (W + 2 * pX) + c * (H + 2 * pY) * (W + 2 * pX) + (h + pY) * (W + 2 * pX) + (w + pX)] = input[b * C * H * W + c * H * W + h * W + w];
                }
            }
        }
    }

    // 进行卷积操作
    for (int b = 0; b < B; ++b) {
        for (int c_out = 0; c_out < oC; ++c_out) {
            for (int h_out = 0; h_out < oH; ++h_out) {
                for (int w_out = 0; w_out < oW; ++w_out) {
                    float sum = 0.0f;

                    for (int c_in = 0; c_in < C; ++c_in) {
                        for (int k_h = 0; k_h < kY; ++k_h) {
                            for (int k_w = 0; k_w < kX; ++k_w) {
                                int h_in = h_out * sY + k_h;
                                int w_in = w_out * sX + k_w;

                                sum += padded_input[b * C * (H + 2 * pY) * (W + 2 * pX) + c_in * (H + 2 * pY) * (W + 2 * pX) + h_in * (W + 2 * pX) + w_in] *
                                       kernel[c_out * C * kY * kX + c_in * kY * kX + k_h * kX + k_w];
                            }
                        }
                    }

                    output[b * oC * oH * oW + c_out * oH * oW + h_out * oW + w_out] = sum + bias[c_out];
                }
            }
        }
    }
#endif
    return 0;
}
