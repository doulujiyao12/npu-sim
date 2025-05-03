#include "common/pd.h"
#include "prims/pd_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int matmul_forward_pd::task() { return 0; }

int matmul_forward_pd::task_core(TaskCoreContext &context) {
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
    int data_size_out = B * T * OC;

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

        printf("[INFO] Matmul_f_pd: read from dram, label: %s\n",
               datapass_label.indata[0].c_str());

        AddrPosKey inp_key =
            AddrPosKey(*sram_addr, data_byte * data_size_input);
        sram_pos_locator->addPair(datapass_label.indata[0], inp_key, context,
                                  dram_time);
    } else {
        AddrPosKey inp_key;
        int flag = sram_pos_locator->findPair(datapass_label.indata[0],
                                              inp_sram_offset);
        printf("[INFO] Matmul_f_pd: read from sram, label: %s, value: %d\n",
               datapass_label.indata[0].c_str(), inp_sram_offset);
        if (flag == -1) {
            printf(
                "[ERROR] Matmul_f_pd: sram_pos_locator cannot find the label: "
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

    printf("Matmul_f_pd: dram time 1: %ld\n", dram_time);

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

    // 写入kvcache，根据batchInfo确定
    for (auto stage : batchInfo) {
        int size = data_byte * B * C * stage.token_num;

        char format_label_k[100];
        sprintf(format_label_k, "%s%skREQ%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%svREQ%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;

        // 如果没有对应的kvcache，则创建一个标签；如果已经有了，则直接更新大小
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_k, size, context, dram_time);
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_v, size, context, dram_time);
    }

    // 决定是否终止（需要放在别的原语中）   
    decode_done->clear();
    for (auto stage : batchInfo) {
        if (stage.type == DECODE && rand_result(5)) {
            decode_done->push_back(true);
        } else {
            decode_done->push_back(false);
        }
    }

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
    AddrPosKey out_key = AddrPosKey(*sram_addr, data_byte * data_size_out / 3);
    sram_pos_locator->addPair(datapass_label.outdata, out_key, context,
                              dram_time);
    sram_write_append_generic(context, data_byte * data_size_out, overlap_time);
#else
#endif
    return overlap_time;
}

void matmul_forward_pd::print_self(string prefix) {
    cout << prefix << "<matmul_forward_pd>\n";
}

void matmul_forward_pd::initialize() {
    out_size = B * T * OC;
    p_inp_size = B * T * C;
    inp_size = B * T * C + OC * C + OC;
}

void matmul_forward_pd::parse_json(json j) {
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

int matmul_forward_pd::sram_utilization(DATATYPE datatype) {
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

    total_sram = p_inp_sram + w1_inps_sram + b_sram + out_sram;

    return total_sram;
}

void matmul_forward_pd::deserialize(sc_bv<128> buffer) {
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

sc_bv<128> matmul_forward_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xc0);
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