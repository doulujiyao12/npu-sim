#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Split_matmul::print_self(string prefix) {
    cout << prefix << "<split_matmul>\n";
    cout << prefix << "\tslice: " << slice << ", dim: " << dim << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset << ", input_offset: " << inp_offset << endl;
}

void Split_matmul::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);

    dim = j["dim"];
    slice = j["slice"];
    // DAHU TODO
    inp_size = B * T * C;
    out_size = B * T * C;
    p_inp_size = inp_size;

    if (dim == 1) {
        out_dim.push_back(B);
        out_dim.push_back(T / slice);
        out_dim.push_back(C);
    } else if (dim == 2) {
        out_dim.push_back(B);
        out_dim.push_back(T);
        out_dim.push_back(C);
    }

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

void Split_matmul::parse_matmul(Matmul_f *p) {
    B = p->B, T = p->T, C = p->C;

    inp_size = B * T * C;
    out_size = B * T * C;
    p_inp_size = inp_size;

    if (dim == 1) {
        out_dim.push_back(B);
        out_dim.push_back(T);
        out_dim.push_back(C);
    } else if (dim == 2) {
        out_dim.push_back(B);
        out_dim.push_back(T);
        out_dim.push_back(C / slice);
    }
}

void Split_matmul::deserialize(sc_bv<128> buffer) {
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

sc_bv<128> Split_matmul::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xc);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(91, 88) = sc_bv<4>(dim);
    d.range(95, 92) = sc_bv<4>(slice);

    return d;
}
int Split_matmul::task_core(TaskCoreContext &context) {
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

    int data_size_input = B * T * C;
    int data_size_output = B * T * C;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

#if USE_SRAM == 1
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

        printf("[INFO] Split_matmul_f: read from dram, label: %s\n", datapass_label.indata[0].c_str());

        AddrPosKey inp_key = AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context, dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0], inp_sram_offset);
        printf("[INFO] Split_matmul_f: read from sram, label: %s, value: %d\n", datapass_label.indata[0].c_str(), inp_sram_offset);
        if (flag == -1) {
            printf("[ERROR] Split_matmul_f: sram_pos_locator cannot find the "
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

    printf("split_matmul_forward: dram time 1: %ld\n", dram_time);

    // sram_read_generic(context, data_byte*data_size_input, inp_sram_offset,
    // dram_time);

    // 删除标签
    if (!input_reuse) {
        sram_pos_locator->deletePair(datapass_label.indata[0]);
    }

    printf("split_matmul_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do DRAM only
#endif

    u_int64_t cycle = 0;
    if (tile_exu.type == MAC_Array) {
        // cycle = (B*T*C) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) *
        // CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
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
    auto temp_out_sram_offset = *sram_addr;

    std::vector<std::string> out_labels;

    std::istringstream iss(datapass_label.outdata);
    std::string label;
    while (iss >> label) {
        out_labels.push_back(label);
    }

    sram_write_append_generic(context, data_byte * data_size_output, overlap_time);

    auto interval = (*sram_addr - temp_out_sram_offset) / out_labels.size();

    for (int i = 0; i < out_labels.size(); i++) {
        AddrPosKey out_key = AddrPosKey(temp_out_sram_offset + i * interval, data_byte * data_size_output / out_labels.size());
        sram_pos_locator->addPair(out_labels[i], out_key, context, dram_time);
    }
#else
    // CTODO: do dram only
#endif
    printf("split_matmul_forward: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}

int Split_matmul::task() {
    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (B * T * C) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = (B*T*C)/(2*16*16);

    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    u_int64_t dram_time = 0;
    int data_byte = 4;

    u_int64_t overlap_time = 0;
    u_int64_t dcacheline;

    if (dim == 2) {
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    u_int64_t out_l = out_global_addr + (b * T * C + t * C + c) * 4;
                    u_int64_t inp_l = inp_global_addr + (b * T * C + t * C + c) * 4;

                    dcacheline = (out_l >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);

                    dcacheline = (inp_l >> dcache_words_in_line_log2) >> 2;
                    dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                }
            }
        }

        overlap_time = dram_time;
    }

    else if (dim == 1) {
        int offset = 0;
        for (int column = 0; column < slice; column++) {
            for (int b = 0; b < B; b++) {
                for (int t = 0; t < T; t++) {
                    for (int c = 0; c < C / slice; c++) {
                        u_int64_t out_l = out_global_addr + offset * 4;
                        u_int64_t inp_l = inp_global_addr + (column * B * T * C / slice + b * T * C / slice + t * C / slice + c) * 4;
                        offset++;

                        dcacheline = (out_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);

                        dcacheline = (inp_l >> dcache_words_in_line_log2) >> 2;
                        dram_time += check_dcache(0, 0, dcacheline << (dcache_words_in_line_log2 + 2), dram_time, time_fetched, time_prefetched, prefetch_tag, false);
                    }
                }
            }
        }

        overlap_time = dram_time;
    }

    /* -----------------------------------------------------------------------
     */


#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp = dram_start + inp_offset;
    float *out = dram_start + out_offset;
    if (dim == 2) {
        // 将in复制到out即可，无需重排
        for (int b = 0; b < B; b++) {
            for (int t = 0; t < T; t++) {
                for (int c = 0; c < C; c++) {
                    out[b * T * C + t * C + c] = inp[b * T * C + t * C + c];
                }
            }
        }
    } else if (dim == 1) {
        int offset = 0;
        for (int column = 0; column < slice; column++) {
            for (int b = 0; b < B; b++) {
                for (int t = 0; t < T; t++) {
                    for (int c = 0; c < C / slice; c++) {
                        out[offset++] = inp[column * B * T * C / slice + b * T * C / slice + t * C / slice + c];
                    }
                }
            }
        }
    }

#endif


    return overlap_time;
}

int Split_matmul::sram_utilization(DATATYPE datatype) { return 0; }