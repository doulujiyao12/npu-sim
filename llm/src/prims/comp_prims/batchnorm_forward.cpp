#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/system_utils.h"

void Batchnorm_f::print_self(string prefix) {
    cout << prefix << "<batchnorm_forward>\n";
    cout << prefix << "\tB: " << B << ", H: " << H << ", W: " << W
         << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << input_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Batchnorm_f::parseJson(json j) {
    B = GetDefinedParam(j["B"]);
    H = GetDefinedParam(j["H"]);
    W = GetDefinedParam(j["W"]);
    C = GetDefinedParam(j["C"]);

    out_size = B * H * W * C;
    input_size = B * H * W * C;
    inp_size = B * H * W * C + C + C;

    if (j.contains("dram_address")) {
        parseAddress(j["dram_address"]);
    }


    // if (inp_offset == -1 && data_offset != -1){
    //     inp_offset = (data_offset * 1024 - B * T * C) / 1024;
    // }
    // if (out_offset == -1 && data_offset != -1){
    //     out_offset = (data_offset * 1024 + OC * C + OC) / 1024;
    // }
    // // 添加以下三行以打印相关信息
    // cout << "\033[1;33m" << "matmul" << "\033[0m" << endl;
    // cout << "inp_offset: " << inp_offset << endl;
    // cout << "out_offset: " << out_offset << endl;
    // cout << "data_offset: " << data_offset << endl;

    if (j.contains("sram_address")) {
        parseSramLabel(j["sram_address"]);
    }
}

int Batchnorm_f::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    if (datatype == DATATYPE::FP16) {
        total_sram = 2 * (out_size + inp_size);
    } else if (datatype == DATATYPE::INT8) {
        total_sram = out_size + inp_size;
    }

    return total_sram;
}

sc_bv<128> Batchnorm_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(BATCHNORM_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(H);
    d.range(87, 72) = sc_bv<16>(W);
    d.range(103, 88) = sc_bv<16>(C);
    d.range(105, 104) = sc_bv<2>(datatype);

    return d;
}

void Batchnorm_f::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    H = buffer.range(71, 56).to_uint64();
    W = buffer.range(87, 72).to_uint64();
    C = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());

    gamma_offset = B * H * W * C + inp_offset;
    beta_offset = C + gamma_offset;
}
int Batchnorm_f::task_core(TaskCoreContext &context) { return 0; }
int Batchnorm_f::task() {
    // TODO DAHU dcache time ???

#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *input = dram_start + inp_offset;
    float *output = dram_start + out_offset;
    float *gamma = dram_start + gamma_offset;
    float *beta = dram_start + beta_offset;

    float epsilon = 1e-5f;
    // 计算每个通道的均值和标准差
    for (int c = 0; c < C; ++c) {
        // 计算均值
        float mean = 0.0f;
        for (int b = 0; b < B; ++b) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    int index = b * (C * H * W) + c * (H * W) + h * W + w;
                    mean += input[index];
                }
            }
        }
        mean /= (B * H * W);

        // 计算标准差
        float variance = 0.0f;
        for (int b = 0; b < B; ++b) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    int index = b * (C * H * W) + c * (H * W) + h * W + w;
                    variance += (input[index] - mean) * (input[index] - mean);
                }
            }
        }
        variance /= (B * H * W);
        float stddev = std::sqrt(variance + epsilon);

        // 执行归一化，缩放和平移
        for (int b = 0; b < B; ++b) {
            for (int h = 0; h < H; ++h) {
                for (int w = 0; w < W; ++w) {
                    int index = b * (C * H * W) + c * (H * W) + h * W + w;
                    output[index] =
                        gamma[c] * ((input[index] - mean) / stddev) + beta[c];
                }
            }
        }
    }
#endif
    return 0;
}
