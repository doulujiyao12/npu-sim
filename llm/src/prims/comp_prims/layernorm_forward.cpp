#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

void Layernorm_f::print_self(string prefix) {
    cout << prefix << "<layernorm_forward>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset << ", input_offset: " << inp_offset << endl;
}

void Layernorm_f::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);

    out_size = B * T * C;
    p_inp_size = B * T * C;
    inp_size = B * T * C + C + C;

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

int Layernorm_f::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;
    int data_byte = 0;

    if (datatype == DATATYPE::FP16) {
        data_byte = 2;
    } else if (datatype == DATATYPE::INT8) {
        data_byte = 1;
    }

    int p_inp_sram = ceiling_division(B * T * C * data_byte * 8, SRAM_BITWIDTH);
    int w_sram = ceiling_division(C * data_byte * 8, SRAM_BITWIDTH);
    int b_sram = ceiling_division(C * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, SRAM_BITWIDTH);

    total_sram = p_inp_sram + w_sram + b_sram + out_sram;

    return total_sram;
}

void Layernorm_f::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    datatype = DATATYPE(buffer.range(89, 88).to_uint64());

    w_offset = B * T * C + inp_offset;
    b_offset = C + w_offset;
}

sc_bv<128> Layernorm_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x1);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(89, 88) = sc_bv<2>(datatype);

    return d;
}

int Layernorm_f::task_core(TaskCoreContext &context) {
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
    int data_size_weight = C;
    int data_size_bias = C;
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
        sram_first_write_generic(context, data_byte * data_size_input, inp_global_addr, dram_time, dram_start);

        size_t space_pos = datapass_label.indata[0].find(' ');
        if (space_pos != std::string::npos) {
            datapass_label.indata[0] = datapass_label.indata[0].substr(space_pos + 1);
        }

        printf("[INFO] Layernorm_f: read from dram, label: %s\n", datapass_label.indata[0].c_str());

        AddrPosKey inp_key = AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context, dram_time);
    } else {
        AddrPosKey inp_key;
        bool flag = sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] Layernorm_f: sram_pos_locator cannot find the "
                   "label: %s\n",
                   datapass_label.indata[0].c_str());
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

    cout << "[Layernorm_f] prefix: " << prefix << endl;

    auto label_weight = ETERNAL_PREFIX + prefix + "_w";
    AddrPosKey w_key;
    int flag = sram_pos_locator->findPair(label_weight, w_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_weight, weight_global_addr, dram_time, dram_start);

        w_key = AddrPosKey(*sram_addr, data_byte * data_size_weight);
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    } else if (flag > 0) {
        sram_first_write_generic(context, flag, inp_global_addr, dram_time, dram_start);
        w_key.size = data_byte * data_size_weight;
        w_key.spill_size = 0;
        sram_pos_locator->addPair(label_weight, w_key, context, dram_time);
    }

    auto label_bias = ETERNAL_PREFIX + prefix + "_b";
    AddrPosKey b_key;
    flag = sram_pos_locator->findPair(label_bias, b_key);
    if (flag == -1) {
        sram_first_write_generic(context, data_byte * data_size_bias, bias_global_addr, dram_time, dram_start);

        AddrPosKey b_key = AddrPosKey(*sram_addr, data_byte * data_size_bias);
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    } else if (flag > 0) {
        sram_first_write_generic(context, flag, bias_global_addr, dram_time, dram_start);
        b_key.size = data_byte * data_size_bias;
        b_key.spill_size = 0;
        sram_pos_locator->addPair(label_bias, b_key, context, dram_time);
    }

    printf("layernorm_forward: dram time 1: %ld\n", dram_time);

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

    printf("layernorm_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do dram only
#endif

    // 计算overlap
    int cycle = 0;
    if (tile_sfu.type == Linear) {
        cycle = B * T * (8 * C + 5) / (tile_sfu.x_dims) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = B * T * (8*C+5) / (2 * 16 * 16);
    u_int64_t overlap_time = 0;

#if USE_SRAM == 1
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
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context, dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#else
    // CTODO: do dram only
#endif
    printf("layernorm_forward: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}
int Layernorm_f::task() {
    int cycle = 0;
    if (tile_sfu.type == Linear) {
        cycle = B * T * (8 * C + 5) / (tile_sfu.x_dims) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = B * T * (8*C+5) / (2 * 16 * 16);
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


    u_int64_t dram_time = 0;


    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    u_int64_t dcacheline;

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            u_int64_t x = inp_global_addr + (b * T + t) * C * 4;
            for (int i = 0; i < C; i++) {
                dcacheline = (x >> dcache_words_in_line_log2) >> 2;
                dram_time += check_dcache(0, 0, x, dcacheline << (dcache_words_in_line_log2 + 2), time_fetched, time_prefetched, prefetch_tag, false);
                x += data_byte;
            }

            x = inp_global_addr + (b * T + t) * C * 4;
            for (int i = 0; i < C; i++) {
                dcacheline = (x >> dcache_words_in_line_log2) >> 2;
                dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                x += data_byte;
            }

            u_int64_t out_bt = out_global_addr + (b * T + t) * C * 4;
            u_int64_t weight_bt = weight_global_addr;
            u_int64_t bias_bt = bias_global_addr;
            x = inp_global_addr + (b * T + t) * C * 4;
            for (int i = 0; i < C; i++) {
                dcacheline = (weight_bt >> dcache_words_in_line_log2) >> 2;
                dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                weight_bt += data_byte;

                dcacheline = (bias_bt >> dcache_words_in_line_log2) >> 2;
                dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                bias_bt += data_byte;

                dcacheline = (x >> dcache_words_in_line_log2) >> 2;
                dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                x += data_byte;
            }
        }
    }

    u_int64_t overlap_time = 0;

    if (dram_time > cycle) {
        overlap_time = dram_time;
    } else {
        overlap_time = cycle;
    }

    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            u_int64_t out_bt = out_global_addr + (b * T + t) * C * 4;
            for (int i = 0; i < C; i++) {
                dcacheline = (out_bt >> dcache_words_in_line_log2) >> 2;
                overlap_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), overlap_time, time_fetched, time_prefetched, prefetch_tag, false);
                out_bt += data_byte;
            }
        }
    }

/* ------------------------------------------------------------------ */
#if DUMMY == 1


#else

    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *weight = dram_start + w_offset;
    float *bias = dram_start + b_offset;
    float eps = 1e-5f;
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            // seek to the input position inp[b,t,:]
            float *x = inp + b * T * C + t * C;
            // calculate the mean
            float m = 0.0f;
            for (int i = 0; i < C; i++) {
                m += x[i]; // compute: C
            }
            // mean
            m = m / C; // compute: 1
            // calculate the variance (without any bias correction)
            float v = 0.0f;
            for (int i = 0; i < C; i++) {
                float xshift = x[i] - m; // compute: C
                v += xshift * xshift;    // compute: 2C
            }
            v = v / C; // compute: 1
            // calculate the rstd (reciprocal standard deviation)
            // 标准差倒数
            float s = 1.0f / sqrtf(v + eps); // compute: 3
            // seek to the output position in out[b,t,:]
            float *out_bt = out + b * T * C + t * C;
            for (int i = 0; i < C; i++) {
                float n = (s * (x[i] - m));        // normalize // compute: 2C
                float o = n * weight[i] + bias[i]; // scale and shift // compute: 2C
                out_bt[i] = o;                     // write
            }
            // cache the mean and rstd for the backward pass later
            // mean[b * T + t] = m;
            // rstd[b * T + t] = s;
        }
    }

#endif

    return overlap_time;
}