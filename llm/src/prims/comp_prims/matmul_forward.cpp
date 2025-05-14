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

void Matmul_f::initialize() {
    out_size = B * T * OC;
    p_inp_size = B * T * C;
    inp_size = B * T * C + OC * C + OC;
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
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);

    initialize();

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }
    
    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

int Matmul_f::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    int p_inp_sram = ceiling_division(B * T * C * data_byte * 8, SRAM_BITWIDTH);
    int w1_inps_sram = ceiling_division(OC * C * data_byte * 8, SRAM_BITWIDTH);
    int b_sram = ceiling_division(OC * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, SRAM_BITWIDTH);

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
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
    use_hw = buffer.range(107, 106).to_uint64();

    w_offset = B * T * C + inp_offset;
    b_offset = OC * C + w_offset;

    initialize();
}

sc_bv<128> Matmul_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x2);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    d.range(105, 104) = sc_bv<2>(datatype);
    d.range(107, 106) = sc_bv<2>(use_hw);

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

// 使用 context 访问原变量
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
    u_int64_t weight_global_addr = dram_addr_tile + w_offset * data_byte;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;
#endif

    // TODO vector load
    // DAHU dram_time 不对
    u_int64_t dram_time = 0;


    int data_size_input = B * T * C;
    int data_size_weight = OC * C;
    int data_size_bias = OC;
    int data_size_out = B * T * OC;

    // TODO discard
    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;


#if USE_SRAM == 1
    // 检查是否可以在此原语结束之后立刻释放中间结果
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    auto inp_sram_offset = 0;
    if (datapass_label.indata[0].find(DRAM_LABEL) == 0) {
        sram_first_write_generic(context, data_byte * data_size_input,
                                 inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] =
                datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] Matmul_f: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                              inp_sram_offset);
        printf("[INFO] Matmul_f: read from sram, label: %s, value: %d\n",
               datapass_label.indata[0].c_str(), inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] Matmul_f: sram_pos_locator cannot find the label: "
                   "%s\n",
                   datapass_label.indata[0].c_str());
            sc_stop();
        } else if (flag > 0) {
            sram_first_write_generic(context, flag, inp_global_addr, dram_time,
                                     dram_start);
            inp_key.size = data_byte * data_size_input;
            inp_key.spill_size = 0;
            sram_pos_locator->addPair(datapass_label.indata[0], inp_key,
                                      context, dram_time);
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
    AddrPosKey w_key;
    int flag = sram_pos_locator->findPair(label_weight, w_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_weight,
                                 weight_global_addr, dram_time, dram_start);

        w_key = AddrPosKey(*sram_addr, data_byte * data_size_weight);
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    } else if (flag > 0) {
        // 可能有部分已经spill在dram中了
        sram_first_write_generic(context, flag, inp_global_addr, dram_time,
                                 dram_start);
        w_key.size = data_byte * data_size_weight;
        w_key.spill_size = 0;
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    }

    auto label_bias = ETERNAL_PREFIX + prefix + "_b";
    AddrPosKey b_key;
    flag = sram_pos_locator->findPair(label_bias, b_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_bias,
                                 bias_global_addr, dram_time, dram_start);

        AddrPosKey b_key = AddrPosKey(*sram_addr, data_byte * data_size_bias);
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    } else if (flag > 0) {
        sram_first_write_generic(context, flag, bias_global_addr, dram_time,
                                 dram_start);
        b_key.size = data_byte * data_size_bias;
        b_key.spill_size = 0;
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    }

    printf("matmul_forward: dram time 1: %ld\n", dram_time);

    // 简化读出所有数据即可
    int w_sram_offset, b_sram_offset;
    sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
    sram_pos_locator->findPair(label_weight, w_sram_offset);
    sram_pos_locator->findPair(label_bias, b_sram_offset);
    sram_read_generic(context, data_byte * data_size_input, inp_sram_offset,
                      dram_time);
    sram_read_generic(context, data_byte * data_size_weight, w_sram_offset,
                      dram_time);
    sram_read_generic(context, data_byte * data_size_bias, b_sram_offset,
                      dram_time);

    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("matmul_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do dram only
#endif

    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * C * OC * 2 /
                (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    u_int64_t overlap_time = 0;

#if USE_SRAM == 1
    if (dram_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        std::cout << RED << "cycle: " << cycle << ", dram_time: " << dram_time
                  << RESET << std::endl;

    } else {
        overlap_time = cycle - dram_time;
        std::cout << GREEN << "cycle: " << cycle << ", dram_time: " << dram_time
                  << RESET << std::endl;
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
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#else

    u_int64_t out_dcacheline = 0;

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int oc = 0; oc < OC; oc++) {
                out_dcacheline =
                    (out_global_addr >> dcache_words_in_line_log2) >> 2;
                //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
                // u_int64_t & time_fetched, u_int64_t & time_prefetched,
                // u_int64_t & prefetch_tag, bool prefetch){

                overlap_time += check_dcache(
                    0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2),
                    overlap_time, time_fetched, time_prefetched, prefetch_tag,
                    false);
                out_global_addr += data_byte;
                // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
                // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
                // assert(out_global_addr < dataset_words_per_tile);
#endif
            }
        }
    }
#endif
    printf("output matmul_forward_cycle %ld \n", overlap_time);
    return overlap_time;
}

int Matmul_f::task() {
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;
    u_int64_t weight_global_addr = dram_addr_tile + w_offset * 4;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * 4;


    // TODO vector load
    // DAHU dram_time 不对
    u_int64_t dram_time = 0;
    int data_byte = 0;

    // TODO discard
    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    u_int64_t in_dcacheline = 0;
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int c = 0; c < C; c++) {
                in_dcacheline =
                    (inp_global_addr >> dcache_words_in_line_log2) >> 2;
                //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
                // u_int64_t & time_fetched, u_int64_t & time_prefetched,
                // u_int64_t & prefetch_tag, bool prefetch){

                dram_time += check_dcache(
                    0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2),
                    dram_time, time_fetched, time_prefetched, prefetch_tag,
                    false);
                inp_global_addr += data_byte;

                // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
                // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
                // assert(inp_global_addr < dataset_words_per_tile); //
                // CTODO:???
#endif
            }
        }
    }

    printf("input matmul_forward_cycle %ld \n", dram_time);

    u_int64_t weight_dcacheline = 0;

    for (int oc = 0; oc < OC; oc++) {
        for (int c = 0; c < C; c++) {
            weight_dcacheline =
                (weight_global_addr >> dcache_words_in_line_log2) >> 2;
            //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t &
            // time_fetched, u_int64_t & time_prefetched, u_int64_t &
            // prefetch_tag, bool prefetch){

            dram_time += check_dcache(
                0, 0, weight_dcacheline << (dcache_words_in_line_log2 + 2),
                dram_time, time_fetched, time_prefetched, prefetch_tag, false);
            weight_global_addr += data_byte;
            // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
            // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
            // assert(weight_global_addr < dataset_words_per_tile);
#endif
        }
    }

    printf("weight matmul_forward_cycle %ld \n", dram_time);

    u_int64_t bias_dcacheline = 0;

    for (int oc = 0; oc < OC; oc++) {
        bias_dcacheline = (bias_global_addr >> dcache_words_in_line_log2) >> 2;
        //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer, u_int64_t &
        // time_fetched, u_int64_t & time_prefetched, u_int64_t & prefetch_tag,
        // bool prefetch){

        dram_time += check_dcache(
            0, 0, bias_dcacheline << (dcache_words_in_line_log2 + 2), dram_time,
            time_fetched, time_prefetched, prefetch_tag, false);
        bias_global_addr += data_byte;
        // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
        // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
        // assert(bias_global_addr < dataset_words_per_tile);
#endif
    }

    printf("bias matmul_forward_cycle %ld \n", dram_time);

    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * C * OC * 2 /
                (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }


    u_int64_t overlap_time = 0;

    if (dram_time > cycle) {
        overlap_time = dram_time;
    } else {
        overlap_time = cycle;
    }

    u_int64_t out_dcacheline = 0;

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int oc = 0; oc < OC; oc++) {
                out_dcacheline =
                    (out_global_addr >> dcache_words_in_line_log2) >> 2;
                //(int tX,int tY, u_int64_t dram_addr, u_int64_t timer,
                // u_int64_t & time_fetched, u_int64_t & time_prefetched,
                // u_int64_t & prefetch_tag, bool prefetch){

                overlap_time += check_dcache(
                    0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2),
                    overlap_time, time_fetched, time_prefetched, prefetch_tag,
                    false);
                out_global_addr += data_byte;
                // printf("out_global_addr: %d  dataset_words_per_tile: %d \n",
                // out_global_addr, dataset_words_per_tile);
#ifdef ASSERT
                // assert(out_global_addr < dataset_words_per_tile);
#endif
            }
        }
    }

    printf("output matmul_forward_cycle %ld \n", overlap_time);

#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;
    const int LOOP_UNROLL = 8;
    if (B * T % LOOP_UNROLL != 0) {
        matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

        cout << "matmul_forward_naive" << endl;
        return 0;
    }

// collapse the B and T loops into one and turn it into a strided loop.
// then we can tile the inner loop, and reuse the loaded weight LOOP_UNROLL many
// times
#pragma omp parallel for
    for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
        for (int o = 0; o < OC; o++) {
            // we'll keep LOOP_UNROLL many results in registers
            float result[LOOP_UNROLL];
            // initialize the bias, if it exists
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
            }
            // inner loops. Because we do LOOP_UNROLL steps of inner bt, we can
            // cache the value of weight[i + o * C] and reuse it. we compile
            // with -Ofast, so the compiler will turn the inner loop into FMAs
            for (int i = 0; i < C; i++) {
                float w = weight[i + o * C];
                for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                    int bt = obt + ibt;
                    result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
                }
            }
            // write back results to main memory
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                int bt = obt + ibt;
                out[bt * OC + o] = result[ibt];
            }
        }
    }
#endif
    return overlap_time;
}

int Matmul_f::task_r() {

    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = B * T * C * OC * 2 /
                (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }

#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;


    const int LOOP_UNROLL = 8;
    if (B * T % LOOP_UNROLL != 0) {
        matmul_forward_naive(out, inp, weight, bias, B, T, C, OC);

        cout << "matmul_forward_naive" << endl;
        return 0;
    }

// collapse the B and T loops into one and turn it into a strided loop.
// then we can tile the inner loop, and reuse the loaded weight LOOP_UNROLL many
// times
#pragma omp parallel for
    for (int obt = 0; obt < B * T; obt += LOOP_UNROLL) {
        for (int o = 0; o < OC; o++) {
            // we'll keep LOOP_UNROLL many results in registers
            float result[LOOP_UNROLL];
            // initialize the bias, if it exists
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                result[ibt] = (bias != NULL) ? bias[o] : 0.0f;
            }
            // inner loops. Because we do LOOP_UNROLL steps of inner bt, we can
            // cache the value of weight[i + o * C] and reuse it. we compile
            // with -Ofast, so the compiler will turn the inner loop into FMAs
            for (int i = 0; i < C; i++) {
                float w = weight[i + o * C];
                for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                    int bt = obt + ibt;
                    result[ibt] += inp[bt * C + i] * w; // compute: 2C*OC
                }
            }
            // write back results to main memory
            for (int ibt = 0; ibt < LOOP_UNROLL; ibt++) {
                int bt = obt + ibt;
                out[bt * OC + o] = result[ibt];
            }
        }
    }
#endif
    return cycle;
}