#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Merge_matmul::print_self(string prefix) {
    cout << prefix << "<merge_matmul>\n";
    cout << prefix << "\t[input size] B: " << B << ", T: " << T << ", C: " << C
         << endl;
    cout << prefix << "\t[merge type] " << (dim == 1 ? ("concat") : ("addup"))
         << endl;
    cout << prefix << "\tslice: " << slice << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Merge_matmul::parse_matmul(Matmul_f *p) {
    B = p->B, T = p->T, C = p->C;

    inp_size = B * T * C * slice;
    p_inp_size = inp_size;

    if (dim == 1) {
        out_size = slice * B * T * C;
    } else if (dim == 2) {
        out_size = B * T * C;
    }
}

void Merge_matmul::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);

    dim = j["dim"];
    slice = j["slice"];

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

void Merge_matmul::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    dim = buffer.range(91, 88).to_uint64();
    slice = buffer.range(95, 92).to_uint64();
}

sc_bv<128> Merge_matmul::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MERGE_MATMUL_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(91, 88) = sc_bv<4>(dim);
    d.range(95, 92) = sc_bv<4>(slice);

    return d;
}
int Merge_matmul::task_core(TaskCoreContext &context) {
#if USE_NB_DRAMSYS == 0
    auto wc = context.wc;
#endif
    auto mau = context.mau;
    auto hmau = context.hmau;
    auto &msg_data = context.msg_data;
    auto sram_addr = context.sram_addr;
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
#endif

    u_int64_t dram_time = 0;
    int data_byte = 0;

    int data_size_input = slice * B * T * C;
    int data_size_output = 0;

    if (dim == 1)
        data_size_output = B * T * C;
    else if (dim == 2)
        data_size_output = slice * B * T * C;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

#if USE_SRAM == 1
    // 检查是否可以在此原语结束之后立刻释放中间结果
    bool input_reuse[MAX_SPLIT_NUM];
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        input_reuse[i] = false;
        if (datapass_label.indata[i][0] == '_') {
            input_reuse[i] = true;
            datapass_label.indata[i] = datapass_label.indata[i].substr(1);
        }
    }

    int inp_sram_offset[MAX_SPLIT_NUM];
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        inp_sram_offset[i] = 0;
    }

    int in_label_cnt = 0;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;
        in_label_cnt++;
    }

    for (int i = 0; i < in_label_cnt; i++) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;

        if (datapass_label.indata[i].find(DRAM_LABEL) == 0) {
            inp_sram_offset[i] = *sram_addr;
            sram_first_write_generic(context,
                                     data_byte * data_size_input / in_label_cnt,
                                     inp_global_addr, dram_time, dram_start);

            size_t space_pos = datapass_label.indata[i].find(' ');
            if (space_pos != std::string::npos) {
                datapass_label.indata[i] =
                    datapass_label.indata[i].substr(space_pos + 1);
            }

            printf("[INFO] Merge_matmul_f: read from dram, label: %s\n",
                   datapass_label.indata[i].c_str());

            AddrPosKey inp_key = AddrPosKey(
                inp_sram_offset[i], data_byte * data_size_input / in_label_cnt);
            sram_pos_locator->addPair(datapass_label.indata[i], inp_key,
                                      context, dram_time);
        } else {
            AddrPosKey inp_key;
            int flag = sram_pos_locator->findPair(datapass_label.indata[i],
                                                  inp_sram_offset[i]);
            printf(
                "[INFO] Merge_matmul_f: read from sram, label: %s, value: %d\n",
                datapass_label.indata[i].c_str(), inp_sram_offset[i]);
            if (flag == -1) {
                printf("[ERROR] Merge_matmul_f: sram_pos_locator cannot find "
                       "the label: %s\n",
                       datapass_label.indata[i].c_str());
                sc_stop();
            } else if (flag > 0) {
                sram_first_write_generic(context, flag, inp_global_addr,
                                         dram_time, dram_start);
                inp_key.size = data_byte * data_size_input;
                inp_key.spill_size = 0;
                sram_pos_locator->addPair(datapass_label.indata[i], inp_key,
                                          context, dram_time);
            }
        }
    }

    printf("merge_matmul_forward: dram time 1: %ld\n", dram_time);

    for (int i = 0; i < in_label_cnt; i++) {
        sram_pos_locator->findPair(datapass_label.indata[i],
                                   inp_sram_offset[i]);
        sram_read_generic(context, data_byte * data_size_input / in_label_cnt,
                          inp_sram_offset[i], dram_time);
    }

    // 删除标签
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (!input_reuse[i] && datapass_label.indata[i] != UNSET_LABEL) {
            sram_pos_locator->deletePair(datapass_label.indata[i]);
        }
    }

    printf("merge_matmul_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do DRAM only
#endif

    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (B * T * C) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) *
                CYCLE;
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
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_output);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_output,
                              overlap_time);
#else
    // CTODO: do dram only
#endif
    printf("merge_matmul_forward: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}
int Merge_matmul::task() {


    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (B * T * C) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) *
                CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = (B*T*C)/(2*16*16);
    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    u_int64_t dram_time = 0;

    u_int64_t overlap_time = 0;
    u_int64_t dcacheline;

    if (dim == 2) {
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    for (int s = 0; s < slice; s++) {
                        u_int64_t out_l =
                            out_global_addr + (b * T * C + t * C + c) * 4;
                        u_int64_t inp_l =
                            inp_global_addr +
                            (s * B * T * C + b * T * C + t * C + c) * 4;

                        dcacheline = (out_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);

                        dcacheline = (inp_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);
                    }
                }
            }
        }
        // DAHU TODO overlap_time ?? cycle
        overlap_time = dram_time;
    } else if (dim == 1) {
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    for (int s = 0; s < slice; s++) {
                        int c_index = s * C + c;
                        u_int64_t out_l =
                            out_global_addr +
                            (b * T * C * slice + t * C * slice + c_index) * 4;
                        u_int64_t inp_l =
                            inp_global_addr +
                            (s * B * T * C + b * T * C + t * C + c) * 4;

                        dcacheline = (out_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);

                        dcacheline = (inp_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(
                            0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
                            dram_time, time_fetched, time_prefetched,
                            prefetch_tag, false);
                    }
                }
            }
        }

        overlap_time = dram_time;
    }

    /* -------------------------------------------------------------------------
     */
#if DUMMY == 1


#else

    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    if (dim == 2) {
        // addup
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    for (int s = 0; s < slice; s++) {
                        out[b * T * C + t * C + c] +=
                            inp[s * B * T * C + b * T * C + t * C + c];
                    }
                }
            }
        }
    } else if (dim == 1) {
        // concat
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    for (int s = 0; s < slice; s++) {
                        int c_index = s * C + c;
                        out[b * T * C * slice + t * C * slice + c_index] =
                            inp[s * B * T * C + b * T * C + t * C + c];
                    }
                }
            }
        }
    }
#endif
    return overlap_time;
}

int Merge_matmul::sram_utilization(DATATYPE datatype) { return 0; }