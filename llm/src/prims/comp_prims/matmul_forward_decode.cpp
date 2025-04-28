#include "systemc.h"

#include "common/system.h"
#include "defs/global.h"
#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Matmul_f_decode::print_self(string prefix) {
    cout << prefix << "<matmul_forward_decode>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C
         << ", OC: " << OC << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Matmul_f_decode::initialize() {
    out_size = B * T * C;
    p_inp_size = B * T * C;
    inp_size = B * T * C + OC * C + OC;

    out_dim.push_back(B);
    out_dim.push_back(T);
    out_dim.push_back(C);
}

HardwareTaskConfig *Matmul_f_decode::generate_hw_config() {
    cout << "Matmul_forward_decode does not support systolic array. Problems "
            "may occur when dim size is not multiple of 4."
         << endl;
    return nullptr;
}

void Matmul_f_decode::parse_json(json j) {
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
int Matmul_f_decode::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    if (datatype == DATATYPE::FP16) {
        total_sram = 2 * (out_size + inp_size);
    } else if (datatype == DATATYPE::INT8) {
        total_sram = out_size + inp_size;
    }

    return total_sram;
}
void Matmul_f_decode::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());

    w_offset = B * T * C + inp_offset;
    b_offset = OC * C + w_offset;

    initialize();
}

sc_bv<128> Matmul_f_decode::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x11);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    d.range(105, 104) = sc_bv<2>(datatype);

    return d;
}

int Matmul_f_decode::task_core(TaskCoreContext &context) {
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

    u_int64_t dram_time = 0;


    int data_size_input = B * T * C;
    int data_size_weight = OC * C;
    int data_size_bias = OC;
    int data_size_out = B * T * C;

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

        printf("[INFO] Matmul_f_decode: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        bool flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                               inp_sram_offset);
        printf("[INFO] Matmul_f_decode: read from sram, label: %s, value: %d\n",
               datapass_label.indata[0].c_str(), inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] Matmul_f_decode: sram_pos_locator cannot find the "
                   "label: %s\n",
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

    printf("matmul_forward_decode: dram time 1: %ld\n", dram_time);

    // 读出i、b和w
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

    // 写入kvcache
    for (int batch = 0; batch < B; batch++) {
        AddrPosKey kcache;
        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                batch);
        string label_decode_k = format_label_k;

        flag = sram_pos_locator->findPair(label_decode_k, kcache);
        if (flag == -1) {
            // 理论而言，这里必须要找到对应的pair。因为在运行该算子之前会先运行prefill的matmul，
            // 在这个时候会首先创建对应大小的kcache和vcache的标签，尽管在这个时候里面还没有任何东西。
            // 但为了便于测试，这里直接创建一个新的标签，不会影响最终的性能。
            kcache.pos = *sram_addr;
            sram_write_append_generic(context, data_byte * data_size_out,
                                      dram_time);
            *sram_addr = kcache.pos;

            kcache.size = data_byte * data_size_out;
            sram_pos_locator->addPair(label_decode_k, kcache, context,
                                      dram_time);
        } else {
            // 如果有的话，那么直接修改大小，写入
            kcache.size += data_byte * data_size_out;

            auto temp_sram = *sram_addr;
            sram_write_append_generic(context, data_byte * data_size_out,
                                      dram_time);
            *sram_addr = temp_sram;

            sram_pos_locator->addPair(label_decode_k, kcache, context,
                                      dram_time);
        }

        AddrPosKey vcache;
        char format_label_v[100];
        sprintf(format_label_v, "%s%sv#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                batch);
        string label_decode_v = format_label_v;

        flag = sram_pos_locator->findPair(label_decode_v, vcache);
        if (flag == -1) {
            vcache.pos = *sram_addr;
            sram_write_append_generic(context, data_byte * data_size_out,
                                      dram_time);
            *sram_addr = vcache.pos;

            vcache.size = data_byte * data_size_out;
            sram_pos_locator->addPair(label_decode_v, vcache, context,
                                      dram_time);
        } else {
            // 如果有的话，那么直接修改大小，写入
            vcache.size += data_size_out;

            auto temp_sram = *sram_addr;
            sram_write_append_generic(context, data_byte * data_size_out,
                                      dram_time);
            *sram_addr = temp_sram;

            sram_pos_locator->addPair(label_decode_v, vcache, context,
                                      dram_time);
        }
    }

    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("matmul_forward_decode: dram time 2: %ld\n", dram_time);
#else

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

#endif
    printf("matmul_forward_decode: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}

void Matmul_f_decode::matmul_forward_naive(float *out, const float *inp,
                                           const float *weight,
                                           const float *bias, int B, int T,
                                           int C, int OC) {
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

int Matmul_f_decode::task() {


#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;

    // 这里的input为最新的一个token，weight包括Wq，Wk，Wv

    // 仅计算最后一个token的Q矩阵，K，V矩阵放入kvcache中
    // 在这里，默认T=1，即只有一个token
    // 矩阵乘法 1*C x C*C => out中存放结果B*T*C (1*1*C)
    matmul_forward_naive(out, inp, weight, bias, B, T, C, C);

    // 接下来单独计算最后一个token的K，V矩阵，放入kvcache中。
    // K，每一个token都会为kvcache带来C个新的值
    for (int b = 0; b < B; b++) {
        for (int t = T - 1; t < T; t++) {
            int bt = b * T + t;
            for (int o = C; o < 2 * C; o++) {
                float val = (bias != NULL) ? bias[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += inp[bt * C + i] * weight[o * C + i];
                }
                // out[bt * OC + o] = val;
                // put into kv cache
                KVCache_g.kvcache[KVCache_g.Kstart] = val;
                KVCache_g.Kstart++;
            }
        }
    }

    // V，每一个token都会为kvcache带来C个新的值
    for (int b = 0; b < B; b++) {
        for (int t = T - 1; t < T; t++) {
            int bt = b * T + t;
            for (int o = 2 * C; o < OC; o++) {
                float val = (bias != NULL) ? bias[o] : 0.0f;
                for (int i = 0; i < C; i++) {
                    val += inp[bt * C + i] * weight[o * C + i];
                }
                // out[bt * OC + o] = val;
                // put into kv cache
                KVCache_g.kvcache[KVCache_g.Vstart] = val;
                KVCache_g.Vstart++;
            }
        }
    }
#endif
    // CTODO cycles
    return 0;
}