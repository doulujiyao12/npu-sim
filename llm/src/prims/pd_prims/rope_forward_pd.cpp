#include "prims/pd_prims.h"
#include "utils/memory_utils.h"

int rope_forward_pd::task() { return 0; }

int rope_forward_pd::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_sincos = B * (C / NH) * 2;

    // 只有Q输出，则B * T * C = 2Q / R + Q
    int data_size_out = B * T * C / (1 + 2 / R); // Q

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t sincos_global_addr = inp_global_addr + sc_offset * data_byte;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;

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
    // cout << "rope data input size: " << data_size_input << endl;
    checkInputData(context, dram_time, inp_global_addr, data_size_input);
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    // 此时默认已经分好注意力头了。对于每一个注意力头，对应的sincos数据大小均为B
    // * T * (C / NH) (最后一个维度已扩展)
    // 读出需要用到的sincos数据
    int max_token_num = 0;
    for (auto stage : batchInfo)
        max_token_num = max(max_token_num, stage.token_num);
    if (job_type == JOB_DECODE)
        max_token_num = 1;

    data_size_sincos *= max_token_num;

    // 读入sincos数据
    auto label_sincos = ETERNAL_PREFIX + prefix + "_sc";
    checkStaticData(context, dram_time, sincos_global_addr, data_size_sincos,
                      label_sincos);

    // 在这里写回kvcache
    int total_tokens = 0;

    for (auto stage : batchInfo) {
        int size = 0;
        switch (job_type) {
        case JOB_PREFILL:
        case JOB_BOTH:
            size = data_byte * B * C * stage.token_num;
            break;
        case JOB_DECODE:
            size = data_byte * B * C * 1 * chunk;
            break;
        default:
            assert(false && "Unsupported job type");
        }

        total_tokens += stage.token_num;

        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%sv#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;
        // cout << "decode_k: " << label_k << endl;

        // cout << "decode_v: " << label_v << endl;


        // 如果没有对应的kvcache，则创建一个标签；如果已经有了，则直接更新大小
        cout << "[rope_f] Core " << cid << " Ready to add label: " << label_k
             << ", size: " << size << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_k, sram_pos_locator, size, dram_time,
                          cid);
        // cout << "[rope_f] Core " << cid << " already add to label: " <<
        // label_k
        //      << ", size: " << sram_pos_locator->findKeySize(label_k) << endl;

#else
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_k, size, context, dram_time);
#endif
        cout << "[rope_f] Core " << cid << " Ready to add label: " << label_v
             << ", size: " << size << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_v, sram_pos_locator, size, dram_time,
                          cid);
        // cout << "[rope_f] Core " << cid << " already add to label: " <<
        // label_v
        //      << ", size: " << sram_pos_locator->findKeySize(label_v) << endl;
#else
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_v, size, context, dram_time);
#endif
    }

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    BETTER_PRINT(dram_time);
#endif

    // 计算overlap并写回output数据
    writeOutputData(context, 6 * total_tokens * C, 0, dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

sc_bv<128> rope_forward_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(ROPE_FORWARD_PD_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(NH);
    d.range(107, 104) = sc_bv<4>(job_type);
    d.range(111, 108) = sc_bv<4>(R);
    d.range(115, 112) = sc_bv<4>(chunk);
    return d;
}

void rope_forward_pd::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint();
    out_offset = buffer.range(39, 24).to_uint();
    B = buffer.range(55, 40).to_uint();
    T = buffer.range(71, 56).to_uint();
    C = buffer.range(87, 72).to_uint();
    NH = buffer.range(103, 88).to_uint();
    job_type = PD_JOB(buffer.range(107, 104).to_uint());
    R = buffer.range(111, 108).to_uint();
    chunk = buffer.range(115, 112).to_uint();

    initialize();
}

void rope_forward_pd::parseJson(json j) {
    B = GetDefinedParam(j["B"]);
    T = GetDefinedParam(j["T"]);
    C = GetDefinedParam(j["C"]);
    NH = GetDefinedParam(j["NH"]);
    R = GetDefinedParam(j["R"]);
    chunk = GetDefinedParam(j["chunk"]);

    auto job_str = j["job_type"];
    if (job_str == "prefill")
        job_type = JOB_PREFILL;
    else if (job_str == "decode")
        job_type = JOB_DECODE;
    else if (job_str == "both")
        job_type = JOB_BOTH;
    else
        job_type = JOB_BOTH;

    initialize();

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);
}

void rope_forward_pd::print_self(string prefix) {
    cout << prefix << "rope_forward_pd: B=" << B << ", T=" << T << ", C=" << C << endl;
}

int rope_forward_pd::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void rope_forward_pd::initialize() {
    inp_size = B * T * C;
    input_size = inp_size;
    out_size = B * T * C / (1 + 2 / R);

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    sc_offset = inp_offset + B * T * C;
}