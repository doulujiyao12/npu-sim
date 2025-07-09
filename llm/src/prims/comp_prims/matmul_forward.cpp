#include "systemc.h"
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/system.h"
#include "defs/global.h"
#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Matmul_f::print_self(string prefix) {
    
    cout << prefix << "<matmul_forward>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C
         << ", OC: " << OC << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}
void Matmul_f::print_dim(int cid) {
    LOG_VERBOSE(1, cid,"Prim name:" << name << " "  << " <matmul_forward>" );
    LOG_VERBOSE(1, cid,"Prim name:" << name << " "  << "\tB: " << B << ", T: " << T << ", C: " << C << ", OC: " << OC );
    LOG_VERBOSE(1, cid,"Prim name:" << name << " "  << "\tout_size: " << out_size << " , inp_size: " << inp_size << ", previous_inp_size: " << p_inp_size );
    LOG_VERBOSE(1, cid,"Prim name:" << name << " "  << "\toutput_offset: " << out_offset << ", input_offset: " << inp_offset );

}

void Matmul_f::initialize() {
    out_size = B * T * OC;
    p_inp_size = B * T * C;
    inp_size = B * T * C + OC * C + OC;
    dram_inp_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (B * T * OC + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = (OC * C + OC + (DRAM_ALIGN - 1)) / DRAM_ALIGN;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    w_offset = B * T * C + inp_offset;
    b_offset = OC * C + w_offset;
}

HardwareTaskConfig *Matmul_f::generate_hw_config() {
    HardwareTaskConfig *config = new HardwareTaskConfig();
    config->hardware = SYSTOLIC_MATMUL;
#if DUMMY == 1

    assert(false && "Dummy is not supported.");
#else

    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;
    config->data.push_back(inp);
    config->data.push_back(out);
    config->data.push_back(weight);
    config->data.push_back(bias);

    config->args.push_back(B);
    config->args.push_back(T);
    config->args.push_back(C);
    config->args.push_back(OC);
#endif
    return config;
}

void Matmul_f::parse_json(json j) {
    /*
    inp_offset（选填） 可以根据 data_offset 计算，也可以手动设置 inp_offset
    data_offset（必要） matmul 需要指定权重位置
    out_offset（选填）: 可以根据 data_offset 计算，也可以手动设置 out_offset
    */

    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);
    // NH = find_var(j["NH"]);
    // DH = find_var(j["DH"]);
    // R = find_var(j["R"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (inp_offset == -1 && out_offset == -1 && data_offset == -1)
        assert(0 && "no dram address found");

    if (inp_offset == -1 && data_offset != -1)
        inp_offset = (data_offset * 1024 - B * T * C) / 1024;

    if (out_offset == -1 && data_offset != -1)
        out_offset = (data_offset * 1024 + OC * C + OC) / 1024;

    // 添加以下三行以打印相关信息
    cout << "\033[1;33m" << "matmul" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
    cout << "data_offset: " << data_offset << endl;

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int Matmul_f::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int p_inp_sram =
        ceiling_division(B * T * C * data_byte * 8, get_sram_bitwidth(cid));
    int w1_inps_sram =
        ceiling_division(OC * C * data_byte * 8, get_sram_bitwidth(cid));
    int b_sram = ceiling_division(OC * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(out_size * data_byte * 8, get_sram_bitwidth(cid));

    // if (datatype == DATATYPE::FP16) {
    //     total_sram = 2 * (out_size + inp_size);
    // } else if (datatype == DATATYPE::INT8) {
    //     total_sram = out_size + inp_size;
    // }

    total_sram = p_inp_sram + w1_inps_sram + b_sram + out_sram;

    return total_sram;
}

void Matmul_f::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(47, 40).to_uint64();
    T = buffer.range(63, 48).to_uint64();
    C = buffer.range(79, 64).to_uint64();
    OC = buffer.range(95, 80).to_uint64();
    datatype = DATATYPE(buffer.range(97, 96).to_uint64());
    use_hw = buffer.range(99, 98).to_uint64();
    // job_type = PD_JOB(buffer.range(103, 100).to_uint64());
    // NH = buffer.range(111, 104).to_uint64();
    // DH = buffer.range(119, 112).to_uint64();
    // R = buffer.range(127, 120).to_uint64();

    initialize();
}

sc_bv<128> Matmul_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_F_TYPE);
    d.range(47, 40) = sc_bv<8>(B);
    d.range(63, 48) = sc_bv<16>(T);
    d.range(79, 64) = sc_bv<16>(C);
    d.range(95, 80) = sc_bv<16>(OC);
    d.range(97, 96) = sc_bv<2>(datatype);
    d.range(99, 98) = sc_bv<2>(use_hw);
    // d.range(103, 100) = sc_bv<4>(job_type);
    // d.range(111, 104) = sc_bv<8>(NH);
    // d.range(119, 112) = sc_bv<8>(DH);
    // d.range(127, 120) = sc_bv<8>(R);

    return d;
}

void Matmul_f::matmul_forward_naive(float *out, const float *inp,
                                    const float *weight, const float *bias,
                                    int B, int T, int C, int OC) {
#pragma omp parallel for collapse(2)
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            int bt = b * T + t;
            for (int o = 0; o < OC; o++) {
                float val = (bias != NULL) ? bias[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += inp[bt * C + i] * weight[o * C + i];
                }
                out[bt * OC + o] = val;
            }
        }
    }
}
int Matmul_f::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_weight = OC * C;
    int data_size_bias = OC;
    int data_size_out = B * T * OC;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t weight_global_addr = dram_addr_tile + w_offset * data_byte;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;

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
    print_dim(context.cid);

#if USE_SRAM == 1
    {
        auto label_weight = ETERNAL_PREFIX + prefix + "_w";
        check_static_data(context, dram_time, weight_global_addr,
                          data_size_weight, label_weight);

        auto label_bias = ETERNAL_PREFIX + prefix + "_b";
        check_static_data(context, dram_time, bias_global_addr, data_size_bias,
                          label_bias);
        BETTER_PRINT(dram_time);

        // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    // cout << "matmul output data size: " << data_size_out << endl;
    write_output_data(context, B * T * C * OC * 2, 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Matmul_f::task() {
    //     u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
    //     u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    //     u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;
    //     u_int64_t weight_global_addr = dram_addr_tile + w_offset * 4;
    //     u_int64_t bias_global_addr = dram_addr_tile + b_offset * 4;


    //     // TODO vector load
    //     // DAHU dram_time 不对
    //     u_int64_t dram_time = 0;
    //     int data_byte = 0;

    //     // TODO discard
    //     u_int64_t time_fetched = 0;
    //     u_int64_t time_prefetched = 0;
    //     u_int64_t prefetch_tag = 0;

    //     if (datatype == INT8) {
    //         data_byte = 1;
    //     } else if (datatype == FP16) {
    //         data_byte = 2;
    //     }

    //     u_int64_t in_dcacheline = 0;
    //     for (int b = 0; b < B; b++) {
    //         for (int t = 0; t < T; t++) {
    //             for (int c = 0; c < C; c++) {
    //                 in_dcacheline =
    //                     (inp_global_addr >> dcache_words_in_line_log2) >> 2;
    //                 //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
    //                 // u_int64_t & time_fetched, u_int64_t & time_prefetched,
    //                 // u_int64_t & prefetch_tag, bool prefetch){

    //                 dram_time += check_dcache(
    //                     0, 0, in_dcacheline << (dcache_words_in_line_log2 +
    //                     2), dram_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 inp_global_addr += data_byte;

    //                 // printf("out_global_addr: %d  dataset_words_per_tile:
    //                 %d
    //                 // \n", out_global_addr, dataset_words_per_tile);
    // #ifdef ASSERT
    //                 // assert(inp_global_addr < dataset_words_per_tile); //
    //                 // CTODO:???
    // #endif
    //             }
    //         }
    //     }

    //     printf("input matmul_forward_cycle %ld \n", dram_time);

    //     u_int64_t weight_dcacheline = 0;

    //     for (int oc = 0; oc < OC; oc++) {
    //         for (int c = 0; c < C; c++) {
    //             weight_dcacheline =
    //                 (weight_global_addr >> dcache_words_in_line_log2) >> 2;
    //             //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
    //             // u_int64_t &
    //             // time_fetched, u_int64_t & time_prefetched, u_int64_t &
    //             // prefetch_tag, bool prefetch){

    //             dram_time += check_dcache(
    //                 0, 0, weight_dcacheline << (dcache_words_in_line_log2 +
    //                 2), dram_time, time_fetched, time_prefetched,
    //                 prefetch_tag, false);
    //             weight_global_addr += data_byte;
    //             // printf("out_global_addr: %d  dataset_words_per_tile: %d
    //             \n",
    //             // out_global_addr, dataset_words_per_tile);
    // #ifdef ASSERT
    //             // assert(weight_global_addr < dataset_words_per_tile);
    // #endif
    //         }
    //     }

    //     printf("weight matmul_forward_cycle %ld \n", dram_time);

    //     u_int64_t bias_dcacheline = 0;

    //     for (int oc = 0; oc < OC; oc++) {
    //         bias_dcacheline = (bias_global_addr >> dcache_words_in_line_log2)
    //         >> 2;
    //         //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t
    //         &
    //         // time_fetched, u_int64_t & time_prefetched, u_int64_t &
    //         // prefetch_tag, bool prefetch){

    //         dram_time += check_dcache(
    //             0, 0, bias_dcacheline << (dcache_words_in_line_log2 + 2),
    //             dram_time, time_fetched, time_prefetched, prefetch_tag,
    //             false);
    //         bias_global_addr += data_byte;
    //         // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
    //         // out_global_addr, dataset_words_per_tile);
    // #ifdef ASSERT
    //         // assert(bias_global_addr < dataset_words_per_tile);
    // #endif
    //     }

    //     printf("bias matmul_forward_cycle %ld \n", dram_time);

    //     u_int64_t cycle = 0;
    //     if (tile_exu.type == MAC_Array) {
    //         cycle = B * T * C * OC * 2 /
    //                 (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) *
    //                 CYCLE;
    //     } else {
    //         assert(false && "Unsupported tile type");
    //     }


    //     u_int64_t overlap_time = 0;

    //     if (dram_time > cycle) {
    //         overlap_time = dram_time;
    //     } else {
    //         overlap_time = cycle;
    //     }

    //     u_int64_t out_dcacheline = 0;

    //     for (int b = 0; b < B; b++) {
    //         for (int t = 0; t < T; t++) {
    //             for (int oc = 0; oc < OC; oc++) {
    //                 out_dcacheline =
    //                     (out_global_addr >> dcache_words_in_line_log2) >> 2;
    //                 //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
    //                 // u_int64_t & time_fetched, u_int64_t & time_prefetched,
    //                 // u_int64_t & prefetch_tag, bool prefetch){

    //                 overlap_time += check_dcache(
    //                     0, 0, out_dcacheline << (dcache_words_in_line_log2 +
    //                     2), overlap_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 out_global_addr += data_byte;
    //                 // printf("out_global_addr: %d  dataset_words_per_tile:
    //                 %d
    //                 // \n", out_global_addr, dataset_words_per_tile);
    // #ifdef ASSERT
    //                 // assert(out_global_addr < dataset_words_per_tile);
    // #endif
    //             }
    //         }
    //     }

    //     printf("output matmul_forward_cycle %ld \n", overlap_time);

    // #if DUMMY == 1


    // #else
    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *inp = dram_start + inp_offset;
    //     float *out = dram_start + out_offset;
    //     float *weight = dram_start + w_offset;
    //     float *bias = dram_start + b_offset;
    //     const int LOOP_UNROLL = 8;
    //     if (B * T % LOOP_UNROLL != 0) {
    //         matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

    //         cout << "matmul_forward_naive" << endl;
    //         return 0;
    //     }

    // // collapse the B and T loops into one and turn it into a strided loop.
    // // then we can tile the inner loop, and reuse the loaded weight
    // LOOP_UNROLL many
    // // times
    // #pragma omp parallel for
    //     for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
    //         for (int o = 0; o < OC; o++) {
    //             // we'll keep LOOP_UNROLL many results in registers
    //             float result[LOOP_UNROLL];
    //             // initialize the bias, if it exists
    //             for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                 result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
    //             }
    //             // inner loops. Because we do LOOP_UNROLL steps of inner bt,
    //             we
    //             // can cache the value of weight[i + o * C] and reuse it. we
    //             // compile with -Ofast, so the compiler will turn the inner
    //             loop
    //             // into FMAs
    //             for (int i = 0; i < C; i++) {
    //                 float w = weight[i + o * C];
    //                 for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                     int bt = obt + ibt;
    //                     result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
    //                 }
    //             }
    //             // write back results to main memory
    //             for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                 int bt = obt + ibt;
    //                 out[bt * OC + o] = result[ibt];
    //             }
    //         }
    //     }
    // #endif
    //     return overlap_time;

    return 0;
}

int Matmul_f::task_r() {

    //     int cycle = 0;
    //     if (tile_exu.type == MAC_Array) {
    //         cycle = B * T * C * OC * 2 /
    //                 (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) *
    //                 CYCLE;
    //     } else {
    //         assert(false && "Unsupported tile type");
    //     }

    // #if DUMMY == 1


    // #else
    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *inp = dram_start + inp_offset;
    //     float *out = dram_start + out_offset;
    //     float *weight = dram_start + w_offset;
    //     float *bias = dram_start + b_offset;


    //     const int LOOP_UNROLL = 8;
    //     if (B * T % LOOP_UNROLL != 0) {
    //         matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

    //         cout << "matmul_forward_naive" << endl;
    //         return 0;
    //     }

    // // collapse the B and T loops into one and turn it into a strided loop.
    // // then we can tile the inner loop, and reuse the loaded weight
    // LOOP_UNROLL many
    // // times
    // #pragma omp parallel for
    //     for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
    //         for (int o = 0; o < OC; o++) {
    //             // we'll keep LOOP_UNROLL many results in registers
    //             float result[LOOP_UNROLL];
    //             // initialize the bias, if it exists
    //             for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                 result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
    //             }
    //             // inner loops. Because we do LOOP_UNROLL steps of inner bt,
    //             we
    //             // can cache the value of weight[i + o * C] and reuse it. we
    //             // compile with -Ofast, so the compiler will turn the inner
    //             loop
    //             // into FMAs
    //             for (int i = 0; i < C; i++) {
    //                 float w = weight[i + o * C];
    //                 for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                     int bt = obt + ibt;
    //                     result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
    //                 }
    //             }
    //             // write back results to main memory
    //             for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
    //                 int bt = obt + ibt;
    //                 out[bt * OC + o] = result[ibt];
    //             }
    //         }
    //     }
    // #endif
    //     return cycle;

    return 0;
}