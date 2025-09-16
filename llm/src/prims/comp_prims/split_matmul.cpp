#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Split_matmul::print_self(string prefix) {
    cout << prefix << "<split_matmul>\n";
    cout << prefix << "\tslice: " << slice << ", dim: " << dim << endl;
    cout << prefix << "\tout_size: " << out_size << " , inp_size: " << inp_size
         << ", previous_inp_size: " << input_size << endl;
    cout << prefix << "\toutput_offset: " << out_offset
         << ", input_offset: " << inp_offset << endl;
}

void Split_matmul::initialize() {
    inp_size = B * T * C;
    out_size = B * T * C;
    input_size = inp_size;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    inp_offset *= 1024;
    out_offset *= 1024;
}

void Split_matmul::parseJson(json j) {
    B = GetDefinedParam(j["B"]);
    T = GetDefinedParam(j["T"]);
    C = GetDefinedParam(j["C"]);

    dim = j["dim"];
    slice = j["slice"];

    // DAHU TODO
    initialize();

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);
}

void Split_matmul::parse_matmul(Matmul_f *p) {
    B = p->B, T = p->T, C = p->C;

    inp_size = B * T * C;
    out_size = B * T * C;
    input_size = inp_size;
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

    initialize();
}

sc_bv<128> Split_matmul::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SPLIT_MATMUL_TYPE);
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
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    vector<int> data_size_input = {B * T * C};
    int data_size_out = B * T * C;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;
    u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;

    // 检查数据重利用
    bool input_reuse = false;
    if (datapass_label.indata[0][0] == '_') {
        input_reuse = true;
        datapass_label.indata[0] = datapass_label.indata[0].substr(1);
    }

    // 获取前缀label
    // 此原语不支持获取前缀，也无需获取

    // 读入input数据
    checkInputData(context, dram_time, inp_global_addr, data_size_input);
    BETTER_PRINT(dram_time);

#if USE_SRAM == 1
    {
        // 删除标签
        if (!input_reuse)
            sram_pos_locator->deletePair(datapass_label.indata[0]);

        BETTER_PRINT(dram_time);
    }
#endif

    // 计算overlap并写回output数据
    writeOutputData(context, 0, B * T * (8 * C + 5), dram_time, overlap_time,
                      data_size_out, out_global_addr);
    BETTER_PRINT(overlap_time);

    return overlap_time;
}

int Split_matmul::task() {
    //     int cycle = 0;
    //     if (tile_exu.type == MAC_Array) {
    //         cycle = (B * T * C) / (tile_exu.x_dims * tile_exu.y_dims *
    //         comp_util) *
    //                 CYCLE;
    //     } else {
    //         assert(false && "Unsupported tile type");
    //     }
    //     // cycle = (B*T*C)/(2*16*16);

    //     u_int64_t dram_addr_tile = cid * dataset_words_per_tile;
    //     u_int64_t out_global_addr = dram_addr_tile + out_offset * 4;
    //     u_int64_t inp_global_addr = dram_addr_tile + inp_offset * 4;

    //     u_int64_t time_fetched = 0;
    //     u_int64_t time_prefetched = 0;
    //     u_int64_t prefetch_tag = 0;

    //     u_int64_t dram_time = 0;
    //     int data_byte = 4;

    //     u_int64_t overlap_time = 0;
    //     u_int64_t dcacheline;

    //     if (dim == 2) {
    //         for (int b = 0; b < B; b++) {
    //             for (int t = 0; t < T; t++) {
    //                 for (int c = 0; c < C; c++) {
    //                     u_int64_t out_l =
    //                         out_global_addr + (b * T * C + t * C + c) * 4;
    //                     u_int64_t inp_l =
    //                         inp_global_addr + (b * T * C + t * C + c) * 4;

    //                     dcacheline = (out_l >> dcache_words_in_line_log2) >>
    //                     2; dram_time += check_dcache(
    //                         0, 0, dcacheline << (dcache_words_in_line_log2 +
    //                         2), dram_time, time_fetched, time_prefetched,
    //                         prefetch_tag, false);

    //                     dcacheline = (inp_l >> dcache_words_in_line_log2) >>
    //                     2; dram_time += check_dcache(
    //                         0, 0, dcacheline << (dcache_words_in_line_log2 +
    //                         2), dram_time, time_fetched, time_prefetched,
    //                         prefetch_tag, false);
    //                 }
    //             }
    //         }

    //         overlap_time = dram_time;
    //     }

    //     else if (dim == 1) {
    //         int offset = 0;
    //         for (int column = 0; column < slice; column++) {
    //             for (int b = 0; b < B; b++) {
    //                 for (int t = 0; t < T; t++) {
    //                     for (int c = 0; c < C / slice; c++) {
    //                         u_int64_t out_l = out_global_addr + offset * 4;
    //                         u_int64_t inp_l =
    //                             inp_global_addr +
    //                             (column * B * T * C / slice + b * T * C /
    //                             slice +
    //                              t * C / slice + c) *
    //                                 4;
    //                         offset++;

    //                         dcacheline = (out_l >> dcache_words_in_line_log2)
    //                         >> 2; dram_time += check_dcache(
    //                             0, 0, dcacheline <<
    //                             (dcache_words_in_line_log2 + 2), dram_time,
    //                             time_fetched, time_prefetched, prefetch_tag,
    //                             false);

    //                         dcacheline = (inp_l >> dcache_words_in_line_log2)
    //                         >> 2; dram_time += check_dcache(
    //                             0, 0, dcacheline <<
    //                             (dcache_words_in_line_log2 + 2), dram_time,
    //                             time_fetched, time_prefetched, prefetch_tag,
    //                             false);
    //                     }
    //                 }
    //             }
    //         }

    //         overlap_time = dram_time;
    //     }

    //     /*
    //     -----------------------------------------------------------------------
    //      */


    // #if DUMMY == 1


    // #else
    //     float *dram_start = (float *)(dram_array[cid]);
    //     float *inp = dram_start + inp_offset;
    //     float *out = dram_start + out_offset;
    //     if (dim == 2) {
    //         // 将in复制到out即可，无需重排
    //         for (int b = 0; b < B; b++) {
    //             for (int t = 0; t < T; t++) {
    //                 for (int c = 0; c < C; c++) {
    //                     out[b * T * C + t * C + c] = inp[b * T * C + t * C +
    //                     c];
    //                 }
    //             }
    //         }
    //     } else if (dim == 1) {
    //         int offset = 0;
    //         for (int column = 0; column < slice; column++) {
    //             for (int b = 0; b < B; b++) {
    //                 for (int t = 0; t < T; t++) {
    //                     for (int c = 0; c < C / slice; c++) {
    //                         out[offset++] =
    //                             inp[column * B * T * C / slice + b * T * C /
    //                             slice +
    //                                 t * C / slice + c];
    //                     }
    //                 }
    //             }
    //         }
    //     }

    // #endif


    //     return overlap_time;

    return 0;
}

int Split_matmul::sram_utilization(DATATYPE datatype, int cid) { return 0; }