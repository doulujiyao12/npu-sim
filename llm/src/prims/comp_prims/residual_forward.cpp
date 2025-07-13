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

    dram_inp_size = (2 * N + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (N + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = 0;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    inp2_offset = N * data_byte + inp_offset;
}


void Residual_f::parse_json(json j) {
    N = find_var(j["N"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (inp_offset == -1)
        inp_offset = (out_offset * 1024 - 2 * N) / 1024;

    if (out_offset == -1)
        assert(0 && "Residual_f: out_offset not set");

    // 添加以下三行以打印相关信息
    cout << "\033[1;33m" << "Residual_f" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;


    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int Residual_f::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    int inp_sram =
        ceiling_division(2 * N * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram = ceiling_division(N * data_byte * 8, get_sram_bitwidth(cid));

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
}

sc_bv<128> Residual_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(RESIDUAL_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(71, 40) = sc_bv<32>(N);
    d.range(73, 72) = sc_bv<2>(datatype);

    return d;
}

int Residual_f::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input;
    int data_size_single_input = N;
    int data_size_out = N;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    u_int64_t inp1_global_addr = dram_addr_tile + inp_offset * data_byte;

    // 检查数据重利用
    bool input_reuse[MAX_SPLIT_NUM];
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        input_reuse[i] = false;
        if (datapass_label.indata[i][0] == '_') {
            input_reuse[i] = true;
            datapass_label.indata[i] = datapass_label.indata[i].substr(1);
        }
    }

    // 获取前缀label
    std::size_t pos = datapass_label.outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos)
        prefix = datapass_label.outdata.substr(0, pos);
    else
        prefix = datapass_label.outdata;

    int in_label_cnt = 0;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        if (datapass_label.indata[i] == UNSET_LABEL)
            continue;
        in_label_cnt++;
    }

    for (int i = 0; i < in_label_cnt; i++)
        data_size_input.push_back(data_size_single_input);

    // 读入input数据
    check_input_data(context, dram_time, inp1_global_addr, data_size_input);
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    {
        // 删除标签
        for (int i = 0; i < MAX_SPLIT_NUM; i++) {
            if (!input_reuse[i] && datapass_label.indata[i] != UNSET_LABEL)
                sram_pos_locator->deletePair(datapass_label.indata[i]);
        }

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    write_output_data(context, N, 0, dram_time, overlap_time, data_size_out,
                      out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Residual_f::task() {
    //     int cycle = 0;
    //     if (tile_exu.type == MAC_Array) {
    //         cycle = (N) / (tile_exu.x_dims * tile_exu.y_dims * comp_util) *
    //         CYCLE;
    //     } else {
    //         assert(false && "Unsupported tile type");
    //     }
    //     // cycle = N / (2 * 16 * 16);

    //     int data_byte = 0;
    //     if (datatype == INT8) {
    //         data_byte = 1;
    //     } else if (datatype == FP16) {
    //         data_byte = 2;
    //     }

    //     u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
    //     u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    //     u_int64_t inp1_global_addr = dram_addr_tile + inp_offset * data_byte;
    //     u_int64_t inp2_global_addr = dram_addr_tile + inp2_offset *
    //     data_byte;

    //     u_int64_t time_fetched = 0;
    //     u_int64_t time_prefetched = 0;
    //     u_int64_t prefetch_tag = 0;

    //     u_int64_t dram_time = 0;
    //     u_int64_t in_dcacheline, out_dcacheline;

    //     for (int i = 0; i < N; i++) {
    //         in_dcacheline = (inp1_global_addr >> dcache_words_in_line_log2)
    //         >> 2; dram_time += check_dcache(
    //             0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2),
    //             dram_time, time_fetched, time_prefetched, prefetch_tag,
    //             false);
    //         in_dcacheline += data_byte;

    //         in_dcacheline = (inp2_global_addr >> dcache_words_in_line_log2)
    //         >> 2; dram_time += check_dcache(
    //             0, 0, in_dcacheline << (dcache_words_in_line_log2 + 2),
    //             dram_time, time_fetched, time_prefetched, prefetch_tag,
    //             false);
    //         in_dcacheline += data_byte;
    //     }

    //     u_int64_t overlap_time = 0;

    //     if (dram_time > cycle) {
    //         overlap_time = dram_time;
    //     } else {
    //         overlap_time = cycle;
    //     }

    //     for (int i = 0; i < N; i++) {
    //         out_dcacheline = (out_global_addr >> dcache_words_in_line_log2)
    //         >> 2; overlap_time += check_dcache(
    //             0, 0, out_dcacheline << (dcache_words_in_line_log2 + 2),
    //             overlap_time, time_fetched, time_prefetched, prefetch_tag,
    //             false);
    //         out_dcacheline += data_byte;
    //     }

    //     /*
    //     ---------------------------------------------------------------------
    //     */

    // #if DUMMY == 1


    // #else
    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *inp1 = dram_start + inp_offset;
    //     float *inp2 = dram_start + inp2_offset;
    //     float *out = dram_start + out_offset;
    //     for (int i = 0; i < N; i++) {
    //         out[i] = inp1[i] + inp2[i]; // compute: N
    //     }
    // #endif
    //     cout << "residual" << endl;

    //     return overlap_time;

    return 0;
}