#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Residual_f::print_self(string prefix) {
    cout << prefix << "<residual_forward>\n";
    cout << prefix << "\tN: " << N << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset
         << ", input2_offset: " << inp2_offset << endl;
}

void Residual_f::initialize() {
    out_size = N;
    p_inp_size = 2 * N;
    inp_size = 2 * N;
}

void Residual_f::parse_json(json j) {
    N = find_var(j["N"]);

    initialize();

    if (j.contains("dram_address")) {
        parse_address(j["dram_address"]);
    }

    if (j.contains("sram_address")) {
        parse_sram_label(j["sram_address"]);
    }
}

int Residual_f::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;
    int data_byte = 0;

    if (datatype == DATATYPE::FP16) {
        data_byte = 2;
    } else if (datatype == DATATYPE::INT8) {
        data_byte = 1;
    }

    int inp_sram = ceiling_division(2 * N * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(N * data_byte * 8, SRAM_BITWIDTH);

    total_sram = inp_sram + out_sram;

    return total_sram;
}

void Residual_f::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    N = buffer.range(71, 40).to_uint64();
    datatype = DATATYPE(buffer.range(73, 72).to_uint64());

    initialize();

    inp2_offset = N + inp_offset;
}

sc_bv<128> Residual_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0x5);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(71, 40) = sc_bv<32>(N);
    d.range(73, 72) = sc_bv<2>(datatype);


    return d;
}

int Residual_f::task_core(TaskCoreContext &context) {
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
    u_int64_t inp1_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t inp2_global_addr = dram_addr_tile + inp2_offset * data_byte;

#if DUMMY == 1
    float *dram_start = nullptr;
#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp1 = dram_start + inp_offset;
    float *inp2 = dram_start + inp2_offset;
    float *out = dram_start + out_offset;
#endif

    u_int64_t dram_time = 0;

    int data_size_input = 2 * N;
    int data_size_single_input = N;
    int data_size_out = N;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

#if USE_SRAM == 1
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
    assert(in_label_cnt == 2 && "residual has two input");
    for (int i = 0; i < in_label_cnt;) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;

        if (datapass_label.indata[i].find(DRAM_LABEL) == 0) {
            inp_sram_offset[i] = *sram_addr;
            

            size_t space_pos = datapass_label.indata[i].find(' ');
            if (space_pos != std::string::npos) {
                datapass_label.indata[i] =
                    datapass_label.indata[i].substr(space_pos + 1);
            }

            printf("[INFO] Residual_f: read from dram, label: %s\n",
                   datapass_label.indata[i].c_str());
#if USE_SRAM_MANAGER == 1
            sram_first_write_generic(context, data_byte * data_size_single_input,
                inp1_global_addr, dram_time, dram_start, datapass_label.indata[i], true, sram_pos_locator);
#else
            sram_first_write_generic(context,
                                     data_byte * data_size_single_input,
                                     inp1_global_addr, dram_time, dram_start);

            AddrPosKey inp_key = AddrPosKey(inp_sram_offset[i],
                                            data_byte * data_size_single_input);
            sram_pos_locator->addPair(datapass_label.indata[i], inp_key,
                                      context, dram_time);
#endif 
        } else {
            AddrPosKey inp_key;
            int flag = sram_pos_locator->findPair(datapass_label.indata[i],
                                                  inp_sram_offset[i]);
            if (flag == -1) {
                printf("[ERROR] Residual_f: sram_pos_locator cannot find the "
                       "label: %s\n",
                       datapass_label.indata[i].c_str());
                sc_stop();
            } else if (flag > 0) {
#if USE_SRAM_MANAGER == 1
            sram_first_write_generic(context, flag, inp1_global_addr, dram_time,
                dram_start, datapass_label.indata[i], true, sram_pos_locator);

#else
                sram_first_write_generic(context, flag, inp1_global_addr,
                                         dram_time, dram_start);
                inp_key.size = data_byte * data_size_single_input;
                inp_key.spill_size = 0;
                sram_pos_locator->addPair(datapass_label.indata[i], inp_key,
                                          context, dram_time);
#endif
            }else{
#if USE_SRAM_MANAGER == 1
            AddrPosKey inp_key;
            int flag =
                sram_pos_locator->findPair(datapass_label.indata[i], inp_key);
            if (inp_key.alloc_id == 0){
            sram_first_write_generic(context, data_byte * data_size_single_input, inp1_global_addr, dram_time,
                dram_start, datapass_label.indata[i], true, sram_pos_locator, true);
            }
#endif

        }
        }

        i++;
    }

    printf("residual_forward: dram time 1: %ld\n", dram_time);

    // 读出inp
    for (int i = 0; i < in_label_cnt; i++) {
#if USE_SRAM_MANAGER == 1
        AddrPosKey input_key;
        sram_pos_locator->findPair(datapass_label.indata[i], input_key);
        sram_pos_locator->printAllKeysWithAllocId();
        sram_read_generic(context, data_byte * data_size_single_input, inp_sram_offset[i],
            dram_time, input_key.alloc_id, true, sram_pos_locator);
#else
        sram_pos_locator->findPair(datapass_label.indata[i],
                                   inp_sram_offset[i]);
        sram_read_generic(context, data_byte * data_size_single_input,
                          inp_sram_offset[i], dram_time);
#endif
    }

    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (!input_reuse[i] && datapass_label.indata[i] != UNSET_LABEL) {
            sram_pos_locator->deletePair(datapass_label.indata[i]);
        }
    }

    printf("residual_forward: dram time 2: %ld\n", dram_time);
#else
    // CTODO: do dram only
#endif

    // 计算overlap
    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (N) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = N / (2 * 16 * 16);
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

#if USE_SRAM_MANAGER == 1
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time,
        datapass_label.outdata, true, sram_pos_locator);
      

#else
    // 写入out

    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#endif
#else
    // CTODO: do dram only
#endif
    printf("residual_forward: overlap_time: %ld\n", overlap_time);
    return overlap_time;
}

int Residual_f::task() {
    int cycle = 0;
    if (tile_exu.type == MAC_Array) {
        cycle = (N) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) * CYCLE;
    } else {
        assert(false && "Unsupported tile type");
    }
    // cycle = N / (2 * 16 * 16);

    int data_byte = 0;
    if (datatype == INT8) {
        data_byte = 1;
    } else if (datatype == FP16) {
        data_byte = 2;
    }

    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp1_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t inp2_global_addr = dram_addr_tile + inp2_offset * data_byte;

    u_int64_t time_fetched = 0;
    u_int64_t time_prefetched = 0;
    u_int64_t prefetch_tag = 0;

    u_int64_t dram_time = 0;
    u_int64_t in_dcacheline, out_dcacheline;

    for (int i = 0; i < N; i++) {
        in_dcacheline = (inp1_global_addr >> dcache_words_in_line_log2) >> 2;
        dram_time += check_dcache(
            0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2), dram_time,
            time_fetched, time_prefetched, prefetch_tag, false);
        in_dcacheline += data_byte;

        in_dcacheline = (inp2_global_addr >> dcache_words_in_line_log2) >> 2;
        dram_time += check_dcache(
            0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2), dram_time,
            time_fetched, time_prefetched, prefetch_tag, false);
        in_dcacheline += data_byte;
    }

    u_int64_t overlap_time = 0;

    if (dram_time > cycle) {
        overlap_time = dram_time;
    } else {
        overlap_time = cycle;
    }

    for (int i = 0; i < N; i++) {
        out_dcacheline = (out_global_addr >> dcache_words_in_line_log2) >> 2;
        overlap_time += check_dcache(
            0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2),
            overlap_time, time_fetched, time_prefetched, prefetch_tag, false);
        out_dcacheline += data_byte;
    }

    /* --------------------------------------------------------------------- */

#if DUMMY == 1


#else
    float *dram_start = (float *)(dram_array[cid]);
    float *inp1 = dram_start + inp_offset;
    float *inp2 = dram_start + inp2_offset;
    float *out = dram_start + out_offset;
    for (int i = 0; i < N; i++) {
        out[i] = inp1[i] + inp2[i]; // compute: N
    }
#endif
    cout << "residual" << endl;

    return overlap_time;
}