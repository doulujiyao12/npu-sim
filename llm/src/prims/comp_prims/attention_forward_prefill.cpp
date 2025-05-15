#include "systemc.h"

#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

void Attention_f_prefill::print_self(string prefix) {
    cout << prefix << "<attention_forward>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Attention_f_prefill::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    NH = find_var(j["NH"]);

    out_size = B * T * C;
    p_inp_size = B * T * 3 * C;
    inp_size = B * T * 3 * C + 2 * B * NH * T * T;

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    // if (inp_offset == -1){
    //     assert(0 && "attention_forward: inp_offset not set");
    // }
    
    // 添加以下三行以打印相关信息
    cout << "\033[1;33m" << "attention_forward" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
    cout << "data_offset: " << data_offset << endl;
    

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}
int Attention_f_prefill::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;
    int data_byte = 0;

    if (datatype == DATATYPE::FP16) {
        data_byte = 2;
    } else if (datatype == DATATYPE::INT8) {
        data_byte = 1;
    }

    int p_inp_sram =
        ceiling_division(B * T * 3 * C * data_byte * 8, SRAM_BITWIDTH);
    int a_sram =
        ceiling_division(B * NH * T * T * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, SRAM_BITWIDTH);

    total_sram = p_inp_sram + a_sram + out_sram;

    return total_sram;
}

void Attention_f_prefill::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    NH = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());

    prea_offset = B * T * 3 * C + inp_offset;
    a_offset = B * NH * T * T + prea_offset;
}

sc_bv<128> Attention_f_prefill::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x15);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    d.range(105, 104) = sc_bv<2>(datatype);

    return d;
}

int Attention_f_prefill::task_core(TaskCoreContext &context) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto temp_mau = context.temp_mau;
    auto temp_hmau = context.temp_hmau;
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
    u_int64_t prea_global_addr = dram_addr_tile + prea_offset * data_byte;
    u_int64_t a_global_addr = dram_addr_tile + a_offset * data_byte;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *preatt = dram_start + prea_offset;
    float *att = dram_start + a_offset;
#endif

    u_int64_t dram_time = 0;


    int data_size_input = B * T * 3 * C;   // QKV input
    int data_size_preatt = B * NH * T * T; // preatt
    int data_size_att = B * NH * T * T;    // att
    int data_size_out = B * T * C;         // output

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

        printf("[INFO] Attention_f_prefill: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag =
            sram_pos_locator->findPair(datapass_label.indata[0], inp_key);
        if (flag == -1) {
            printf("[ERROR] Attention_f_prefill: sram_pos_locator cannot find the "
                   "label: %s\n",
                   datapass_label.indata[0].c_str());
            sc_stop();
        } else if (flag > 0) {
            sram_first_write_generic(context, flag, inp_global_addr, dram_time,
                                     dram_start);
            inp_key.size = data_size_input;
            inp_key.spill_size = 0;
            sram_pos_locator->addPair(datapass_label.indata[0], inp_key,
                                      context, dram_time);
        }
    }

    // // 获取前缀label
    // std::size_t pos = datapass_label.outdata.find_last_of('_');
    // std::string prefix;
    // if (pos != std::string::npos) {
    //     prefix = datapass_label.outdata.substr(0, pos);
    // } else {
    //     prefix = datapass_label.outdata;
    // }

    // auto label_preatt = ETERNAL_PREFIX + prefix + "_preatt";
    // AddrPosKey preatt_key =
    //     AddrPosKey(*sram_addr, data_byte * data_size_preatt);
    // sram_pos_locator->addPair(label_preatt, preatt_key, context, dram_time);
    // sram_write_append_generic(context, data_byte * data_size_preatt, dram_time);

    // auto label_att = ETERNAL_PREFIX + prefix + "_att";
    // AddrPosKey att_key = AddrPosKey(*sram_addr, data_byte * data_size_att);
    // sram_pos_locator->addPair(label_att, att_key, context, dram_time);
    // sram_write_append_generic(context, data_byte * data_size_att, dram_time);

    printf("attention_forward: dram time 1: %ld\n", dram_time);

    int cur_tokens = 0;

    // 查找kvcache! 需要使用相应的kvcache label 读出KV
    int batch = 0;
        AddrPosKey kcache;
        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                batch);
        string label_decode_k = format_label_k;

        int flag = sram_pos_locator->findPair(label_decode_k, kcache);
        if (flag == -1) {
            printf("[ERROR] attention_forward_decode: failed to find label %s, "
                   "exit.\n",
                   label_decode_k.c_str());
            sc_stop();
        }

        AddrPosKey vcache;
        char format_label_v[100];
        sprintf(format_label_v, "%s%sv#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                batch);
        string label_decode_v = format_label_v;

        flag = sram_pos_locator->findPair(label_decode_v, vcache);
        if (flag == -1) {
            printf("[ERROR] attention_forward_decode: failed to find label %s, "
                   "exit.\n",
                   label_decode_v.c_str());
            sc_stop();
        }

        // 读出k,v
        sram_read_generic(context, kcache.size, kcache.pos, dram_time);
        sram_read_generic(context, vcache.size, vcache.pos, dram_time);

        cur_tokens = kcache.size / (B * C * data_byte);

    // 读出Q
    sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
    sram_read_generic(context, data_byte * data_size_input / 3,
                      inp_sram_offset, dram_time);
    // 写入preatt中间结果
    int temp_sram_addr = 0;
    int temp_sram_addr_piror = 0;
    temp_sram_addr_piror = temp_sram_addr; 
    sram_write_back_temp(context, data_byte * data_size_preatt,
        temp_sram_addr, dram_time);
    // 读出preatt，计算自然指数，写入att
    sram_read_generic_temp(context, data_byte * data_size_preatt, temp_sram_addr_piror,
                      dram_time);
    temp_sram_addr_piror = temp_sram_addr;
    sram_write_back_temp(context, data_byte * data_size_att, temp_sram_addr,
                            dram_time);
    // 读出att和V
    sram_read_generic_temp(context, data_byte * data_size_att, temp_sram_addr_piror,
                      dram_time);
    
    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("attention_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do dram only
#endif

    // 计算overlap
    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (B * NH * T * (T - 1) / 2 * (4 * C / NH + 5)) /
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
    // CTODO: do dram only
#endif
    printf("attention_forward: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}
int Attention_f_prefill::task() {
    int C3 = C * 3;
    int hs = C / NH; // head size
    float scale = 1.0 / sqrtf(hs);
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t prea_global_addr = dram_addr_tile + prea_offset * data_byte;
    u_int64_t a_global_addr = dram_addr_tile + a_offset * data_byte;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    u_int64_t dram_time = 0;


    u_int64_t in_dcacheline, prea_dcacheline, a_dcacheline, out_dcacheline;

    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (B * NH * T * (T - 1) / 2 * (4 * C / NH + 5)) /
                (2 * tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }


    // TODO!!!
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int h = 0; h < NH; h++) {
                u_int64_t query_t = inp_global_addr +
                                    (b * T * C3 + t * C3 + h * hs) * data_byte;
                u_int64_t preatt_bth =
                    prea_global_addr +
                    (b * NH * T * T + h * T * T + t * T) * data_byte;

                // pass 1
                for (int t2 = 0; t2 <= t; t2++) {
                    u_int64_t key_t2 =
                        inp_global_addr +
                        (b * T * C3 + t2 * C3 + h * hs + C) * data_byte;

                    for (int i = 0; i < hs; i++) {
                        in_dcacheline =
                            (query_t >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0,
                            in_dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);
                        query_t += data_byte;

                        in_dcacheline =
                            (key_t2 >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0,
                            in_dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);
                        key_t2 += data_byte;
                    }


                    prea_dcacheline =
                        (preatt_bth >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(
                        0, 0,
                        prea_dcacheline << (dcache_words_in_line_log2 + 2),
                        dram_time, time_fetched, time_prefetched, prefetch_tag,
                        false);
                    preatt_bth += data_byte;
                }

                u_int64_t att_bth =
                    a_global_addr +
                    (b * NH * T * T + h * T * T + t * T) * data_byte;
                preatt_bth = prea_global_addr +
                             (b * NH * T * T + h * T * T + t * T) * data_byte;

                // pass 2
                for (int t2 = 0; t2 <= t; t2++) {
                    a_dcacheline = (att_bth >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(
                        0, 0, a_dcacheline << (dcache_words_in_line_log2 + 2),
                        dram_time, time_fetched, time_prefetched, prefetch_tag,
                        false);
                    att_bth += data_byte;

                    prea_dcacheline =
                        (preatt_bth >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(
                        0, 0,
                        prea_dcacheline << (dcache_words_in_line_log2 + 2),
                        dram_time, time_fetched, time_prefetched, prefetch_tag,
                        false);
                    preatt_bth += data_byte;
                }

                att_bth = a_global_addr +
                          (b * NH * T * T + h * T * T + t * T) * data_byte;

                // pass 3
                for (int t2 = 0; t2 < T; t2++) {
                    a_dcacheline = (att_bth >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(
                        0, 0, a_dcacheline << (dcache_words_in_line_log2 + 2),
                        dram_time, time_fetched, time_prefetched, prefetch_tag,
                        false);
                    att_bth += data_byte;
                }
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
            for (int h = 0; h < NH; h++) {
                u_int64_t out_bth =
                    out_global_addr + (b * T * C + t * C + h * hs) * data_byte;

                for (int t2 = 0; t2 < T; t2++) {
                    out_dcacheline =
                        (out_bth >> dcache_words_in_line_log2) >> 2;
                    overlap_time += check_dcache(
                        0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2),
                        overlap_time, time_fetched, time_prefetched,
                        prefetch_tag, false);
                    out_bth += data_byte;
                }
            }
        }
    }


    /* -------------------------------------------------------------------------
     */
#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    float *preatt = dram_start + prea_offset;
    float *att = dram_start + a_offset;

#pragma omp parallel for collapse(3)
    for (int b = 0; b < B; b++) {
        for (int t = 0; t < T; t++) {
            for (int h = 0; h < NH; h++) {
                float *query_t = inp + b * T * C3 + t * C3 + h * hs;
                float *preatt_bth = preatt + b * NH * T * T + h * T * T + t * T;
                float *att_bth = att + b * NH * T * T + h * T * T + t * T;

                // pass 1: calculate query dot key and maxval
                float maxval = -10000.0f; // TODO something better
                for (int t2 = 0; t2 <= t; t2++) {
                    float *key_t2 = inp + b * T * C3 + t2 * C3 + h * hs +
                                    C; // +C because it's key

                    // (query_t) dot (key_t2)
                    float val = 0.0f;
                    for (int i = 0; i < hs; i++) {
                        val += query_t[i] *
                               key_t2[i]; // compute: B*NH*T*(T-1)/2*(2*hs+1)
                    }
                    val *= scale;
                    if (val > maxval) {
                        maxval = val;
                    }
                    // scale 后的 QK
                    preatt_bth[t2] = val;
                }

                // pass 2: calculate the exp and keep track of sum
                // maxval is being calculated and subtracted only for numerical
                // stability
                float expsum = 0.0f;
                for (int t2 = 0; t2 <= t; t2++) {
                    float expv = expf(preatt_bth[t2] -
                                      maxval); // compute: B*NH*T*(T-1)/2*3
                    expsum += expv;
                    att_bth[t2] = expv;
                }
                float expsum_inv = expsum == 0.0f ? 0.0f : 1.0f / expsum;

                // pass 3: normalize to get the softmax
                for (int t2 = 0; t2 < T; t2++) {
                    if (t2 <= t) {
                        att_bth[t2] *= expsum_inv; // compute: B*NH*T*(T-1)/2
                    } else {
                        // causal attention mask. not strictly necessary to set
                        // to zero here only doing this explicitly for debugging
                        // and checking to PyTorch
                        att_bth[t2] = 0.0f;
                    }
                }

                // pass 4: accumulate weighted values into the output of
                // attention
                float *out_bth = out + b * T * C + t * C + h * hs;
                for (int i = 0; i < hs; i++) {
                    out_bth[i] = 0.0f;
                }
                for (int t2 = 0; t2 <= t; t2++) {
                    float *value_t2 = inp + b * T * C3 + t2 * C3 + h * hs +
                                      C * 2; // +C*2 because it's value
                    float att_btht2 = att_bth[t2];
                    for (int i = 0; i < hs; i++) {
                        out_bth[i] +=
                            att_btht2 *
                            value_t2[i]; // compute: B*NH*T*(T-1)/2*2*hs
                    }
                }
            }
        }
    }

#endif

    cout << "attention" << endl;
    return overlap_time;
}