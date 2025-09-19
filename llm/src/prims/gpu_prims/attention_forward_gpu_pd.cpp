#include "defs/enums.h"
#include "defs/global.h"
#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"
#include <regex>

void attention_forward_gpu_pd::initialize() {
    auto &p = param_value;
    data_size_input = {data_byte * p["B"] * p["T"] * p["C"]};
    data_chunk = {{"preatt", data_byte * p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"att", data_byte * p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"output", data_byte * p["B"] * p["NH"] * p["T"] * p["C"] /
                                 (slice_x * slice_y)}};
}

int attention_forward_gpu_pd::taskCoreDefault(TaskCoreContext &context) {
    auto &p = param_value;
    p["B"] *= gpu_B;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->indata[0], input_mem_offset)) {
        printf("[ERROR] attention_forward_gpu_pd:gpu_pos_locator cannot find "
               "the label: "
               "%s\n",
               prim_context->datapass_label_->indata[0].c_str());
        sc_stop();
    }

    // 获取前缀label
    std::size_t pos = prim_context->datapass_label_->outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos) {
        prefix = prim_context->datapass_label_->outdata.substr(0, pos);
    } else {
        prefix = prim_context->datapass_label_->outdata;
    }

    auto label_preatt = prefix + "_preatt";
    AddrPosKey p_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "preatt"));
    prim_context->gpu_pos_locator_->fetchPair(label_preatt, p_key);

    auto label_att = prefix + "_att";
    AddrPosKey a_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "att"));
    prim_context->gpu_pos_locator_->fetchPair(label_att, a_key);

    cout << prim_context->cid
         << " [attention_forward_gpu_pd] before read1: " << mem_time
         << " at addr " << input_mem_offset << endl;

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    for (auto stage : prim_context->batch_info_) {
        char format_label_k[1000];

        std::regex pattern("attention"); // 因为是字面量，不需要复杂正则

        // 替换为目标字符串
        std::string result = std::regex_replace(prefix, pattern, "matmul");


        sprintf(format_label_k, "%s%s%sk#%d", result.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, stage.req_id);
        string label_k = format_label_k;

        char format_label_v[1000];
        sprintf(format_label_v, "%s%s%sv#%d", result.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, stage.req_id);
        string label_v = format_label_v;

        AddrPosKey k_key, v_key;
        prim_context->gpu_pos_locator_->findPair(label_k, k_key);
        prim_context->gpu_pos_locator_->findPair(label_v, v_key);
        // LOG_VERBOSE(1, context.cid," atten kv prefix " << prefix << " key
        // size" << k_key.size<< " " << label_k);

        gpu_read_generic(
            context, k_key.pos + k_key.size / (slice_x * slice_y) * fetch_index,
            k_key.size / (slice_x * slice_y), mem_time, true);
        gpu_read_generic(
            context, v_key.pos + v_key.size / (slice_x * slice_y) * fetch_index,
            v_key.size / (slice_x * slice_y), mem_time, true);
    }

    auto data_size_preatt = GetFromPairedVector(data_chunk, "preatt");
    auto data_size_att = GetFromPairedVector(data_chunk, "att");

    gpu_write_generic(context,
                      p_key.pos +
                          data_size_preatt / (slice_x * slice_y) * fetch_index,
                      data_size_preatt / (slice_x * slice_y), mem_time);
    gpu_read_generic(context,
                     p_key.pos +
                         data_size_preatt / (slice_x * slice_y) * fetch_index,
                     data_size_preatt / (slice_x * slice_y), mem_time);

    gpu_write_generic(
        context, a_key.pos + data_size_att / (slice_x * slice_y) * fetch_index,
        data_size_att / (slice_x * slice_y), mem_time);
    gpu_read_generic(
        context, a_key.pos + data_size_att / (slice_x * slice_y) * fetch_index,
        data_size_att / (slice_x * slice_y), mem_time);

    // Q
    gpu_read_generic(context,
                     input_mem_offset +
                         input_size / (3 * slice_x * slice_y) * fetch_index,
                     input_size / (3 * slice_x * slice_y), mem_time);

    // overlap_time = 0;
    AddrPosKey out_key;
    prim_context->gpu_pos_locator_->updatePair(
        prim_context->datapass_label_->outdata,
        GetFromPairedVector(data_chunk, "output"));
    prim_context->gpu_pos_locator_->findPair(
        prim_context->datapass_label_->outdata, out_key);

    gpu_write_generic(context, out_key.pos,
                      GetFromPairedVector(data_chunk, "output"), mem_time);
    int cycle = 0;
    int cid = context.cid;

    CoreHWConfig core_config = GetCoreHWConfig(cid);
    ExuConfig *exu = core_config.exu;
    SfuConfig *sfu = core_config.sfu;

    if (exu->type == MAC_Array)
        cycle += p["B"] * p["NH"] * p["T"] * (p["T"] - 1) / 2 *
                 (4 * p["C"] / p["NH"] + 5) / (slice_x * slice_y) /
                 (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += 0 / (slice_x * slice_y) / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");


    if (mem_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << RED << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);
    } else {
        overlap_time = cycle - mem_time;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << GREEN << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);
    }
#endif

    cout << cid << " [attention_forward_gpu_pd] after write: " << overlap_time
         << endl;

    p["B"] /= gpu_B;

    return overlap_time;
}

GpuBase *attention_forward_gpu_pd::clone() {
    return new attention_forward_gpu_pd(*this);
}