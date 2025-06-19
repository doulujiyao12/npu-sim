#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

void Layernorm_f::print_self(string prefix) {
    cout << prefix << "<layernorm_forward>\n";
    cout << prefix << "\tB: " << B << ", T: " << T << ", C: " << C << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << p_inp_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}


void Layernorm_f::initialize() {
    out_size = B * T * C;
    p_inp_size = B * T * C;
    inp_size = B * T * C + C + C;

    dram_inp_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_out_size = (B * T * C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;
    dram_data_size = (C + C + (DRAM_ALIGN - 1)) / DRAM_ALIGN;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    w_offset = B * T * C + inp_offset;
    b_offset = C + w_offset;
}

void Layernorm_f::parse_json(json j) {
    /*
    inp_offset（选填） 可以根据 data_offset 计算，也可以手动设置 inp_offset
    data_offset（必要） matmul 需要指定权重位置
    out_offset（选填）: 可以根据 data_offset 计算，也可以手动设置 out_offset
    */

    B = find_var(j["B"]);
    T = find_var(j["T"]);
    C = find_var(j["C"]);

    initialize();

    if (j.contains("dram_address"))
        parse_address(j["dram_address"]);

    if (inp_offset == -1 && out_offset == -1 && data_offset == -1)
        assert(0 && "no dram address found");

    if (inp_offset == -1 && data_offset != -1)
        inp_offset = (data_offset * 1024 - B * T * C) / 1024;

    if (out_offset == -1 && data_offset != -1)
        out_offset = (data_offset * 1024 + C + C) / 1024;

    // 添加以下三行以打印相关信息
    cout << "\033[1;33m" << "Layernorm_f" << "\033[0m" << endl;
    cout << "inp_offset: " << inp_offset << endl;
    cout << "out_offset: " << out_offset << endl;
    cout << "data_offset: " << data_offset << endl;

    if (j.contains("sram_address"))
        parse_sram_label(j["sram_address"]);
}

int Layernorm_f::sram_utilization(DATATYPE datatype) {
    int total_sram = 0;

    int p_inp_sram =
        ceiling_division(B * T * C * data_byte * 8, get_sram_bitwidth(cid));
    int w_sram = ceiling_division(C * data_byte * 8, get_sram_bitwidth(cid));
    int b_sram = ceiling_division(C * data_byte * 8, get_sram_bitwidth(cid));
    int out_sram =
        ceiling_division(out_size * data_byte * 8, get_sram_bitwidth(cid));

    total_sram = p_inp_sram + w_sram + b_sram + out_sram;

    return total_sram;
}

void Layernorm_f::deserialize(sc_bv<128> buffer) {
    inp_offset = buffer.range(23, 8).to_uint64();
    inp_offset *= 1024;
    out_offset = buffer.range(39, 24).to_uint64();
    out_offset *= 1024;
    B = buffer.range(55, 40).to_uint64();
    T = buffer.range(71, 56).to_uint64();
    C = buffer.range(87, 72).to_uint64();
    datatype = DATATYPE(buffer.range(89, 88).to_uint64());

    initialize();
}

sc_bv<128> Layernorm_f::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(LAYERNORM_F_TYPE);
    d.range(23, 8) = sc_bv<16>(inp_offset);
    d.range(39, 24) = sc_bv<16>(out_offset);
    d.range(55, 40) = sc_bv<16>(B);
    d.range(71, 56) = sc_bv<16>(T);
    d.range(87, 72) = sc_bv<16>(C);
    d.range(89, 88) = sc_bv<2>(datatype);

    return d;
}

int Layernorm_f::task_core(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_weight = C;
    int data_size_bias = C;
    int data_size_out = B * T * C;

    // dram地址
    u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
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
        auto label_weight = ETERNAL_PREFIX + prefix + "_w";
        check_static_data(context, dram_time, weight_global_addr,
                          data_size_weight, label_weight);

        auto label_bias = ETERNAL_PREFIX + prefix + "_b";
        check_static_data(context, dram_time, bias_global_addr, data_size_bias,
                          label_bias);

        // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    write_output_data(context, 0, B * T * (8 * C + 5), dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Layernorm_f::task() {
    //     int cycle = 0;
    //     if (tile_sfu.type == Linear) {
    //         cycle = B * T * (8 * C + 5) / (tile_sfu.x_dims) * CYCLE;
    //     } else {
    //         assert(false && "Unsupported tile type");
    //     }
    //     // cycle = B * T * (8*C+5) / (2 * 16 * 16);
    //     int data_byte = 0;
    //     if (datatype == INT8) {
    //         data_byte = 1;
    //     } else if (datatype == FP16) {
    //         data_byte = 2;
    //     }
    //     u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
    //     u_int64_t out_global_addr = dram_addr_tile + out_offset * data_byte;
    //     u_int64_t inp_global_addr = dram_addr_tile + inp_offset * data_byte;
    //     u_int64_t weight_global_addr = dram_addr_tile + w_offset * data_byte;
    //     u_int64_t bias_global_addr = dram_addr_tile + b_offset * data_byte;


    //     u_int64_t dram_time = 0;


    //     u_int64_t time_fetched = 0;
    //     u_int64_t time_prefetched = 0;
    //     u_int64_t prefetch_tag = 0;

    //     u_int64_t dcacheline;

    //     for (int b = 0; b < B; b++) {
    //         for (int t = 0; t < T; t++) {
    //             u_int64_t x = inp_global_addr + (b * T + t) * C * 4;
    //             for (int i = 0; i < C; i++) {
    //                 dcacheline = (x >> dcache_words_in_line_log2) >> 2;
    //                 dram_time += check_dcache(
    //                     0, 0, x, dcacheline << (dcache_words_in_line_log2 +
    //                     2), time_fetched, time_prefetched, prefetch_tag,
    //                     false);
    //                 x += data_byte;
    //             }

    //             x = inp_global_addr + (b * T + t) * C * 4;
    //             for (int i = 0; i < C; i++) {
    //                 dcacheline = (x >> dcache_words_in_line_log2) >> 2;
    //                 dram_time += check_dcache(
    //                     0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
    //                     dram_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 x += data_byte;
    //             }

    //             u_int64_t out_bt = out_global_addr + (b * T + t) * C * 4;
    //             u_int64_t weight_bt = weight_global_addr;
    //             u_int64_t bias_bt = bias_global_addr;
    //             x = inp_global_addr + (b * T + t) * C * 4;
    //             for (int i = 0; i < C; i++) {
    //                 dcacheline = (weight_bt >> dcache_words_in_line_log2) >>
    //                 2; dram_time += check_dcache(
    //                     0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
    //                     dram_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 weight_bt += data_byte;

    //                 dcacheline = (bias_bt >> dcache_words_in_line_log2) >> 2;
    //                 dram_time += check_dcache(
    //                     0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
    //                     dram_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 bias_bt += data_byte;

    //                 dcacheline = (x >> dcache_words_in_line_log2) >> 2;
    //                 dram_time += check_dcache(
    //                     0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
    //                     dram_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 x += data_byte;
    //             }
    //         }
    //     }

    //     u_int64_t overlap_time = 0;

    //     if (dram_time > cycle) {
    //         overlap_time = dram_time;
    //     } else {
    //         overlap_time = cycle;
    //     }

    //     for (int b = 0; b < B; b++) {
    //         for (int t = 0; t < T; t++) {
    //             u_int64_t out_bt = out_global_addr + (b * T + t) * C * 4;
    //             for (int i = 0; i < C; i++) {
    //                 dcacheline = (out_bt >> dcache_words_in_line_log2) >> 2;
    //                 overlap_time += check_dcache(
    //                     0, 0, dcacheline << (dcache_words_in_line_log2 + 2),
    //                     overlap_time, time_fetched, time_prefetched,
    //                     prefetch_tag, false);
    //                 out_bt += data_byte;
    //             }
    //         }
    //     }

    // /* ------------------------------------------------------------------ */
    // #if DUMMY == 1


    // #else

    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *inp = dram_start + inp_offset;
    //     float *out = dram_start + out_offset;
    //     float *weight = dram_start + w_offset;
    //     float *bias = dram_start + b_offset;
    //     float eps = 1e-5f;
    //     for (int b = 0; b < B; b++) {
    //         for (int t = 0; t < T; t++) {
    //             // seek to the input position inp[b,t,:]
    //             float *x = inp + b * T * C + t * C;
    //             // calculate the mean
    //             float m = 0.0f;
    //             for (int i = 0; i < C; i++) {
    //                 m += x[i]; // compute: C
    //             }
    //             // mean
    //             m = m / C; // compute: 1
    //             // calculate the variance (without any bias correction)
    //             float v = 0.0f;
    //             for (int i = 0; i < C; i++) {
    //                 float xshift = x[i] - m; // compute: C
    //                 v += xshift * xshift;    // compute: 2C
    //             }
    //             v = v / C; // compute: 1
    //             // calculate the rstd (reciprocal standard deviation)
    //             // 标准差倒数
    //             float s = 1.0f / sqrtf(v + eps); // compute: 3
    //             // seek to the output position in out[b,t,:]
    //             float *out_bt = out + b * T * C + t * C;
    //             for (int i = 0; i < C; i++) {
    //                 float n = (s * (x[i] - m)); // normalize // compute: 2C
    //                 float o =
    //                     n * weight[i] + bias[i]; // scale and shift //
    //                     compute: 2C
    //                 out_bt[i] = o;               // write
    //             }
    //             // cache the mean and rstd for the backward pass later
    //             // mean[b * T + t] = m;
    //             // rstd[b * T + t] = s;
    //         }
    //     }

    // #endif

    //     return overlap_time;

    return 0;
}