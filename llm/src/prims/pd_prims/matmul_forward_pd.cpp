#include "common/pd.h"
#include "prims/pd_prims.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

REGISTER_PRIM(matmul_forward_pd);

void matmul_forward_pd::initialize() {
    auto &p = param_value;
    data_size_input = {p["B"] * p["T"] * p["C"]};
    data_chunk = {{"weight", p["C"] * p["OC"]},
                  {"bias", p["OC"]},
                  {"output", p["B"] * p["T"] * p["OC"] / 3}};
}

int matmul_forward_pd::taskCore(TaskCoreContext &context, string prim_name,
                                u_int64_t dram_time, u_int64_t &exu_ops,
                                u_int64_t &sfu_ops) {
    // 空转一轮，直接退出（PD模式）
    auto &p = param_value;
    if (p["T"] == 0)
        return 0;

    bool need_multiply = false;
    for (auto stage : prim_context->batch_info_) {
        if (stage.type == DECODE) {
            need_multiply = true;
            break;
        }
    }

    int chunk_ratio = need_multiply ? 1 : p["chunk"];

#if NB_CACHE_DEBUG == 1
    LOG_VERBOSE(1, context.cid, " data_size_weight " << data_size_weight);
#endif
    auto label_weight = ETERNAL_PREFIX + prim_name + "_w";
    checkStaticData(context, dram_time, data_chunk_addr["weight"],
                    GetFromPairedVector(data_chunk, "weight") / chunk_ratio,
                    label_weight);

    auto label_bias = ETERNAL_PREFIX + prim_name + "_b";
    checkStaticData(context, dram_time, data_chunk_addr["bias"],
                    GetFromPairedVector(data_chunk, "bias") / chunk_ratio,
                    label_bias);
    ARGUS_PRINT(dram_time);

    // 写入kvcache，根据batchInfo确定
    for (auto stage : prim_context->batch_info_) {
        cout << "[Matmul_pd] Core" << prim_context->cid
             << " stage_type: " << stage.type
             << " token_num: " << stage.token_num << " req_id: " << stage.req_id
             << endl;
        int size = 0;
        switch (p["job_type"]) {
        case JOB_PREFILL:
        case JOB_BOTH:
            size = data_byte * p["B"] * p["OC"] * stage.token_num / 3;
            break;
        case JOB_DECODE:
            size = data_byte * p["B"] * p["OC"] / 3 * p["chunk"];
            break;
        default:
            assert(false && "Unsupported job type");
        }

        char format_label_k[100];
        sprintf(format_label_k, "%s%s%sk#%d", prim_name.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, stage.req_id);
        string label_k = format_label_k;

        char format_label_v[100];
        sprintf(format_label_v, "%s%s%sv#%d", prim_name.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, stage.req_id);
        string label_v = format_label_v;

        // 如果没有对应的kvcache，则创建一个标签；如果已经有了，则直接更新大小
        cout << "[Matmul_pd_f] Core " << prim_context->cid
             << " Ready to add label: " << label_k << ", size: " << size
             << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_k, prim_context->sram_pos_locator_,
                          size, dram_time, cid);
#else
        sram_write_append_generic(context, size, dram_time);
        prim_context->sram_pos_locator_->updatePair(label_k, size, context,
                                                    dram_time);
#endif
        cout << "[Matmul_pd_f] Core " << prim_context->cid
             << " Ready to add label: " << label_v << ", size: " << size
             << endl;

#if USE_SRAM_MANAGER == 1
        sram_update_cache(context, label_v, prim_context->sram_pos_locator_,
                          size, dram_time, cid);
#else
        sram_write_append_generic(context, size, dram_time);
        prim_context->sram_pos_locator_->updatePair(label_v, size, context,
                                                    dram_time);
#endif
    }

    // 决定是否终止（需要放在别的原语中）
    prim_context->decode_done_.clear();
    for (auto stage : prim_context->batch_info_) {
        if (stage.type == DECODE && RandResult(2))
            prim_context->decode_done_.push_back(true);
        else
            prim_context->decode_done_.push_back(false);
    }

#if PERFORMANCE_MODE == 1

    ExuConfig *exu = GetCoreHWConfig(context.cid)->exu;

    uint64_t weight_tile_x = (p["C"] + exu->x_dims - 1) / exu->x_dims;
    uint64_t weight_tile_y = (p["OC"] + exu->y_dims - 1) / exu->y_dims;

    uint64_t padding_input_x =
        (p["B"] * p["T"]) > exu->x_dims ? p["B"] * p["T"] : exu->x_dims;

    uint64_t performance_cycle = (exu->x_dims + exu->x_dims + padding_input_x) *
                                 weight_tile_x * weight_tile_y;

    uint64_t performance_comp =
        performance_cycle * exu->y_dims * exu->x_dims * comp_util;
    LOG_VERBOSE(1, context.cid,
                "Prim name:" << name << " performance_cycle "
                             << performance_cycle);

    int loop_input_count =
        weight_tile_y - 1; // read loop_input_count Repetitive input

    for (int loop = 0; loop < loop_input_count; loop++) {
        for (int p = 0; p < data_size_input.size(); p++) {
            if (prim_context->datapass_label_->indata[p].find(DRAM_LABEL) ==
                0) {

                prefReadData(context, dram_time, data_size_input[p],
                             prim_context->datapass_label_->indata[p]);
            }
        }
    }

    exu_ops = performance_comp;
#else
    exu_ops = (u_int64_t)p["B"] * p["T"] * p["C"] * p["OC"] * 2;
#endif
}
