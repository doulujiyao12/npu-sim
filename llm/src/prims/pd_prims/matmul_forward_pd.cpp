#include "common/pd.h"
#include "prims/pd_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

int matmul_forward_pd::task() { return 0; }

int matmul_forward_pd::task_core(TaskCoreContext &context) {
    // 空转一轮，直接退出（PD模式）
    if (T == 0)
        return 0;

    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_weight = OC * C;
    int data_size_bias = OC;
    int data_size_out = B * T * OC;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    u_int64_t weight_global_addr = dram_addr_tile + w_offset * data_byte;
    u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;

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
    auto label_weight = ETERNAL_PREFIX + prefix + "_w";
    check_static_data(context, dram_time, weight_global_addr, data_size_weight,
                      label_weight);

    auto label_bias = ETERNAL_PREFIX + prefix + "_b";
    check_static_data(context, dram_time, bias_global_addr, data_size_bias,
                      label_bias);
    BETTER_PRINT(dram_time);

    // 写入kvcache，根据batchInfo确定
    for (auto stage : batchInfo) {
        int size = 0;
        switch (job_type) {
        case JOB_PREFILL:
        case JOB_BOTH:
            size = data_byte * B * OC * stage.token_num / 3;
            break;
        case JOB_DECODE:
            size = data_byte * B * OC * 1 / 3;
            break;
        default:
            assert(false && "Unsupported job type");
        }

        char format_label_k[100];
        sprintf(format_label_k, "%s%sk#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%sv#%d", ETERNAL_PREFIX, KVCACHE_PREFIX,
                stage.req_id);
        string label_v = format_label_v;

        // 如果没有对应的kvcache，则创建一个标签；如果已经有了，则直接更新大小
        cout << "[Matmul_pd_f] Core " << cid
             << " Ready to add label: " << label_k << ", size: " << size
             << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_k, sram_pos_locator, size, dram_time, cid);
#else
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_k, size, context, dram_time);
#endif
        cout << "[Matmul_pd_f] Core " << cid
             << " Ready to add label: " << label_v << ", size: " << size
             << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_v, sram_pos_locator, size, dram_time,cid);
#else
        sram_write_append_generic(context, size, dram_time);
        sram_pos_locator->updatePair(label_v, size, context, dram_time);
#endif
    }

    // 决定是否终止（需要放在别的原语中）
    decode_done->clear();
    for (auto stage : batchInfo) {
        if (stage.type == DECODE && rand_result(2))
            decode_done->push_back(true);
        else
            decode_done->push_back(false);
    }

    // 删除标签
    if (!input_reuse)
        sram_pos_locator->deletePair(datapass_label.indata[0]);

    BETTER_PRINT(dram_time);
#endif


#if PERFORMANCE_MODE == 1

    ExuConfig *exu = get_exu_config(context.cid);

    int weight_tile_x = (C + exu->x_dims - 1) / exu->x_dims;   
    int weight_tile_y = (OC + exu->y_dims - 1) / exu->y_dims;

    int padding_input_x = (T * B) > exu->x_dims ? T * B : exu->x_dims;

    int performance_cycle = (exu->x_dims + exu->x_dims + padding_input_x) * weight_tile_x * weight_tile_y;

    int performance_comp = performance_cycle * exu->y_dims * exu->x_dims * comp_util;
    LOG_VERBOSE(1, context.cid,"Prim name:" << name << " performance_cycle " << performance_cycle);

    int loop_input_count = weight_tile_y - 1; // read loop_input_count Repetitive input 

    for (int loop = 0; loop < loop_input_count; loop++){
        for (int p = 0; p < data_size_input.size(); p++) {
            if (datapass_label.indata[p].find(DRAM_LABEL) == 0) {

                perf_read_data(context, dram_time, data_size_input[p], datapass_label.indata[p]);
            }
        }
    }

    

    write_output_data(context, performance_comp, 0, dram_time, overlap_time,
                      data_size_out / 3, out_global_addr);

#else
    // // 计算overlap并写回output数据
    // // cout << "matmul output data size: " << data_size_out << endl;
    // write_output_data(context, B * T * C * OC * 2, 0, dram_time, overlap_time,
    //                   data_size_out / 3, out_global_addr);

    switch (job_type) {

        case JOB_PREFILL:
        case JOB_DECODE:
            write_output_data(context, B * T * C * OC * 2, 0, dram_time, overlap_time,
                      data_size_out / 3, out_global_addr);
            break;
        case JOB_BOTH:
            write_output_data(context, B * T * C * OC * 2, 0, dram_time, overlap_time,
                      data_size_out / 3, out_global_addr);
            break;
        default:
            assert(false && "Unsupported job type");
    }


#endif 


    

    // // 计算overlap并写回output数据
    // write_output_data(context, B * T * C * OC * 2, 0, dram_time, overlap_time,
    //                   data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

void matmul_forward_pd::print_self(string prefix) {
    cout << prefix << "<matmul_forward_pd>\n";
}

void matmul_forward_pd::initialize() {
    out_size = B * T * OC;
    p_inp_size = B * T * C;
    inp_size = B * T * C + OC * C + OC;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    w_offset = B * T * C + inp_offset;
    b_offset = OC * C + w_offset;
}

void matmul_forward_pd::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);

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
        parse_address(j["dram_address"]);

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int matmul_forward_pd::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * T * C * data_byte * 8, get_sram_bitwidth(cid));
    int w1_inps_sram =
        ceiling_division(OC * C * data_byte * 8, get_sram_bitwidth(cid));
    int b_sram = ceiling_division(OC * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(out_size * data_byte * 8, get_sram_bitwidth(cid));

    total_sram = p_inp_sram + w1_inps_sram + b_sram + out_sram;
    total_sram *= get_sram_bitwidth(cid) / 8;
    return total_sram;
}

void matmul_forward_pd::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(47, 40).to_uint64();
    T = buffer.range(63, 48).to_uint64();
    C = buffer.range(79, 64).to_uint64();
    OC = buffer.range(95, 80).to_uint64();
    datatype = DATATYPE(buffer.range(97, 96).to_uint64());
    use_hw = buffer.range(99, 98).to_uint64();
    job_type = PD_JOB(buffer.range(103, 100).to_uint64());
    NH = buffer.range(111, 104).to_uint64();
    DH = buffer.range(119, 112).to_uint64();
    R = buffer.range(127, 120).to_uint64();

    initialize();
}

sc_bv<128> matmul_forward_pd::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_FORWARD_PD_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(47, 40) = sc_bv<8>(B);
    d.range(63, 48) = sc_bv<16>(T);
    d.range(79, 64) = sc_bv<16>(C);
    d.range(95, 80) = sc_bv<16>(OC);
    d.range(97, 96) = sc_bv<2>(datatype);
    d.range(99, 98) = sc_bv<2>(use_hw);
    d.range(103, 100) = sc_bv<4>(job_type);
    d.range(111, 104) = sc_bv<8>(NH);
    d.range(119, 112) = sc_bv<8>(DH);
    d.range(127, 120) = sc_bv<8>(R);

    return d;
}