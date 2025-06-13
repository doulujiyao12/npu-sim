#include "prims/pd_prims.h"
#include "utils/memory_utils.h"

void attention_forward_pd::print_self(string prefix) {
    cout << prefix << "<attention_forward_pd>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void attention_forward_pd::initialize() {
    out_size = B * T * C;
    p_inp_size = B * T * 3 * C;
    inp_size = B * T * 3 * C;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    prea_offset = B * T * 3 * C + inp_offset;
    a_offset = B * NH * T * T + prea_offset;
}

void attention_forward_pd::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    NH = find_var(j["NH"]);

    auto job_str = j["job_type"];
    if (job_str == "prefill")
        job_type = JOB_PREFILL;
    else if (job_str == "decode")
        job_type = JOB_DECODE;
    else if (job_str == "both")
        job_type = JOB_BOTH;
    else
        job_type = JOB_NONE;

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int attention_forward_pd::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * T * 3 * C * data_byte * 8, SRAM_BITWIDTH);
    int a_sram =
        ceiling_division(B * NH * T * T * data_byte * 8, SRAM_BITWIDTH);
    int out_sram = ceiling_division(out_size * data_byte * 8, SRAM_BITWIDTH);

    total_sram = p_inp_sram + a_sram + out_sram;

    return total_sram;
}

void attention_forward_pd::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    NH = buffer.range(103, 88).to_uint64();
    datatype = DATATYPE(buffer.range(105, 104).to_uint64());
    job_type = PD_JOB(buffer.range(111, 108).to_uint64());

    initialize();
}

sc_bv<128> attention_forward_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(ATTENTION_FORWARD_PD_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    d.range(105, 104) = sc_bv<2>(datatype);
    d.range(111, 108) = sc_bv<4>(job_type);

    return d;
}

int attention_forward_pd::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_input = B * T * 3 * C;   // QKV input
    int data_size_preatt = B * NH * T * T; // preatt
    int data_size_att = B * NH * T * T;    // att
    int data_size_out = B * T * C;         // output

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile * 4;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t prea_global_addr = dram_addr_tile + prea_offset * data_byte;
    u_int64_t a_global_addr = dram_addr_tile + a_offset * data_byte;

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

#if USE_SRAM == 1
    int cur_tokens = 0;

    // 查找kvcache! 需要使用相应的kvcache label 读出KV
    // 根据batchInfo进行，逻辑和普通prefill和decode相同
    for (auto stage : batchInfo) {
        int batch = stage.req_id;

        AddrPosKey kcache;
        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                batch);
        string label_decode_k = format_label_k;

        int flag = sram_pos_locator->findPair(label_decode_k, kcache);
        if (flag == -1) {
            printf("[ERROR] attention_forward_pd: failed to find label %s, "
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
            printf("[ERROR] attention_forward_pd: failed to find label %s, "
                   "exit.\n",
                   label_decode_v.c_str());
            sc_stop();
        }

        // 读出k,v
        sram_read_generic(context, kcache.size, kcache.pos, dram_time);
        sram_read_generic(context, vcache.size, vcache.pos, dram_time);

        cur_tokens = kcache.size / (B * C * data_byte);
    }

    // 写入preatt中间结果
    int temp_sram_addr = 0;
    int temp_sram_addr_prior = 0;
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context, data_byte * data_size_preatt, temp_sram_addr,
                         dram_time);
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;

    // 读出preatt，计算自然指数，写入att
    sram_read_generic_temp(context, data_byte * data_size_preatt,
                           temp_sram_addr_prior, dram_time);
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context, data_byte * data_size_att, temp_sram_addr,
                         dram_time);
    // 读出att
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_read_generic_temp(context, data_byte * data_size_att,
                           temp_sram_addr_prior, dram_time);

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    BETTER_PRINT(dram_time);
#endif

    // 计算overlap并写回output数据
    write_output_data(context, B * NH * T * (T - 1) / 2 * (4 * C / NH + 5), 0,
                      dram_time, overlap_time, data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int attention_forward_pd::task() { return 0; }