#include "prims/moe_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void matmul_forward_moe::print_self(string prefix) {
    cout << prefix << "<matmul_forward_moe>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C
         << ", OC: " << OC << endl;
    cout << prefix << "\tout_size: " << out_size << ", inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << ", weight_offset: " << w_offset
         << ", bias_offset: " << b_offset << endl;
}

void matmul_forward_moe::initialize() {
    out_size = B * T * OC;
    p_inp_size = B * T * C;
    inp_size = B * T * C;

    dram_inp_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (B * T * OC + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = 0;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    w_offset = B * T * C * E_N + inp_offset;
    b_offset = w_offset + C * OC * E_N;
}

void matmul_forward_moe::parse_json(json j) {
    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);
    OC = find_var(j["OC"]);
    E_N = find_var(j["E_N"]); // 专家个数
    K = find_var(j["K"]);     // 选取的专家个数

    if (j.contains("choose"))
        need_choose = j["choose"].get<bool>();
    else
        need_choose = false;

    if (j.contains("is_merge"))
        is_merge = j["is_merge"].get<bool>();
    else
        is_merge = false;

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (inp_offset == -1)
        inp_offset = (out_offset * 1024 - B * T * C) / 1024;

    if (out_offset == -1)
        assert(0 && "matmul_forward_moe: out_offset not set");

    cout << "\033[1;33m" << "matmul_forward_moe" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int matmul_forward_moe::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * T * C * data_byte * 8, get_sram_bitwidth(cid));
    int w_sram =
        ceiling_division(B * T * C * K * data_byte * 8, get_sram_bitwidth(cid));
    int b_sram =
        ceiling_division(OC * K * data_byte * 8, get_sram_bitwidth(cid));

    int out_sram = ceiling_division(B * T * OC * K * data_byte * 8,
                                    get_sram_bitwidth(cid));

    total_sram += p_inp_sram + w_sram + b_sram + out_sram;

    return total_sram;
}

void matmul_forward_moe::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    OC = buffer.range(103, 88).to_uint64();
    E_N = buffer.range(111, 104).to_uint64();
    K = buffer.range(119, 112).to_uint64();
    datatype = DATATYPE(buffer.range(121, 120).to_uint64());
    need_choose = buffer.range(123, 122).to_uint64();
    is_merge = buffer.range(125, 124).to_uint64();

    initialize();
}

sc_bv<128> matmul_forward_moe::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(MATMUL_FORWARD_MOE_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(103, 88) = sc_bv<16>(OC);
    d.range(111, 104) = sc_bv<8>(E_N);
    d.range(119, 112) = sc_bv<8>(K);
    d.range(121, 120) = sc_bv<2>(datatype);
    d.range(123, 122) = sc_bv<2>(need_choose);
    d.range(125, 124) = sc_bv<2>(is_merge);

    return d;
}

int matmul_forward_moe::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input; // QKV input
    int data_size_weight_single = OC * C;
    int data_size_bias_single = OC;
    int data_size_out;

    if (is_merge) {
        data_size_input.push_back(B * T * C * K);
        data_size_out = B * T * OC;
    } else {
        data_size_input.push_back(B * T * C);
        data_size_out = B * T * OC * K;
    }

    // dram地址
    u_int64_t dram_addr_tile = 0; // cid * dataset_words_per_tile;
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
    {
        // 判断是否需要重选专家
        if (need_choose) {
            if (selected_experts->size() != K)
                selected_experts->clear();

            cout << "[MOE] Core " << cid << ": Selecting experts..." << endl;

            bool exp_flag[E_N];
            for (auto &b : exp_flag)
                b = false;

            for (auto e : *selected_experts)
                exp_flag[e] = true;

            for (auto &e : *selected_experts) {
                if (rand_result(50))
                    continue; // 50%概率不重选

                exp_flag[e] = false;
                do {
                    e = rand() % E_N;
                } while (exp_flag[e]);
                exp_flag[e] = true;
            }

            for (int i = selected_experts->size(); i < K; i++) {
                int s_exp;
                do {
                    s_exp = rand() % E_N;
                } while (exp_flag[s_exp]);
                exp_flag[s_exp] = true;
                selected_experts->push_back(s_exp);
            }

            while (selected_freq->size() < E_N)
                selected_freq->push_back(0);

            for (auto e : *selected_experts)
                (*selected_freq)[e]++;

        } else {
            if (selected_experts->size() != K) {
                cout << "[ERROR] selected_experts size mismatch: "
                     << selected_experts->size() << " != " << K << endl;
                sc_stop();
                return 0;
            }
        }

        for (auto e : *selected_experts) {
            cout << "selected expert: " << e << endl;
        }

        // 优先查看是否有被prefetch的专家
        bool checked[selected_experts->size()];
        for (int i = 0; i < selected_experts->size(); i++)
            checked[i] = false;

        for (auto e : *selected_experts) {
            if (std::find(prefetched_experts->begin(),
                          prefetched_experts->end(),
                          e) == prefetched_experts->end())
                continue;

            auto label_weight = ETERNAL_PREFIX + prefix + "_w_" + to_string(e);
            check_static_data(context, dram_time,
                              weight_global_addr + e * data_size_weight_single,
                              data_size_weight_single, label_weight);

            auto label_bias = ETERNAL_PREFIX + prefix + "_b_" + to_string(e);
            check_static_data(context, dram_time,
                              bias_global_addr + e * data_size_bias_single,
                              data_size_bias_single, label_bias);

            checked[e] = true;
        }

        for (auto e : *selected_experts) {
            if (checked[e])
                continue;

            auto label_weight = ETERNAL_PREFIX + prefix + "_w_" + to_string(e);
            check_static_data(context, dram_time,
                              weight_global_addr + e * data_size_weight_single,
                              data_size_weight_single, label_weight);

            auto label_bias = ETERNAL_PREFIX + prefix + "_b_" + to_string(e);
            check_static_data(context, dram_time,
                              bias_global_addr + e * data_size_bias_single,
                              data_size_bias_single, label_bias);

            checked[e] = true;
        }

        BETTER_PRINT(dram_time);

        // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    int flops;
    if (is_merge)
        flops = B * T * C * OC * 2 * K + B * T * OC * K;
    else
        flops = B * T * C * OC * 2 * K;
#if PERFORMANCE_MODE == 1

    ExuConfig *exu = get_exu_config(context.cid);
    
    int weight_tile_x = (C + exu->x_dims - 1) / exu->x_dims;   
    int weight_tile_y = (OC + exu->y_dims - 1) / exu->y_dims;

    int padding_input_x = (T * B * K) > exu->x_dims ? T * B * K: exu->x_dims;

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
                      data_size_out, out_global_addr);

#else
    // 计算overlap并写回output数据
    write_output_data(context, flops, 0, dram_time, overlap_time, data_size_out,
                      out_global_addr);
#endif 
    
    BETTER_PRINT(overlap_time);

    return 0;
}

int matmul_forward_moe::task() { return 0; }