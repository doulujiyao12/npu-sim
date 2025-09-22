#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void matmul_forward_gpu_pd::initialize() {
    auto &p = param_value;
    input_size = {data_byte * p["B"] * p["T"] * p["C"]};
    data_chunk = {{"weight", data_byte * p["C"] * p["OC"]},
                  {"bias", data_byte * p["C"]},
                  {"output", data_byte * p["B"] * p["T"] * p["oC"] /
                                 (3 * slice_x * slice_y)}};
}

int matmul_forward_gpu_pd::taskCoreDefault(TaskCoreContext &context) {
    auto &p = param_value;
    p["B"] *= gpu_B;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->indata[0], input_mem_offset)) {
        printf("[ERROR] matmul_forward_gpu_pd: prim_context->gpu_pos_locator_ "
               "cannot find the "
               "label: "
               "%s\n",
               prim_context->datapass_label_->indata[0].c_str());
        sc_stop();
    }

    // 获取前缀label
    cout << "[GPU MATMUL PDS]: output label: "
         << prim_context->datapass_label_->outdata << endl;
    std::size_t pos = prim_context->datapass_label_->outdata.find_last_of('_');
    std::string prefix;
    if (pos != std::string::npos) {
        prefix = prim_context->datapass_label_->outdata.substr(0, pos);
    } else {
        prefix = prim_context->datapass_label_->outdata;
    }
    cout << "[GPU MATMUL PDS]: prefix: " << prefix << endl;

    auto label_weight = prefix + "_w";
    AddrPosKey w_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "weight"));
    prim_context->gpu_pos_locator_->fetchPair(label_weight, w_key);

    auto label_bias = prefix + "_b";
    AddrPosKey b_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "bias"));
    prim_context->gpu_pos_locator_->fetchPair(label_bias, b_key);

    cout << prim_context->cid
         << " [matmul_forward_gpu_pd] before read1: " << mem_time << " at addr "
         << input_mem_offset << endl;

    int overlap_time = 0;
    AddrPosKey out_key;

#if USE_L1L2_CACHE == 1
    if (gpu_inner == true) {
        // 通过fetch_index计算位置
        int row_index = fetch_index / slice_x;
        int col_index = fetch_index % slice_x;

        // input 读入
        gpu_read_generic(
            context, input_mem_offset + input_size / slice_y * row_index,
            input_size / slice_y, mem_time);
#if GPU_CACHE_DEBUG == 1

        cout << " data_size_weight / slice_x " << data_size_weight / slice_x
             << endl;

#endif
        // weight 读入

        gpu_read_generic(context, w_key.pos + w_key.size / slice_x * col_index,
                         GetFromPairedVector(data_chunk, "weight") / slice_x,
                         mem_time);
        // bias 读入
        gpu_read_generic(context, b_key.pos + b_key.size / slice_x * col_index,
                         GetFromPairedVector(data_chunk, "bias") / slice_x,
                         mem_time);

        for (auto stage : prim_context->batch_info_) {
            int size = 0;
            switch (p["job_type"]) {
            case JOB_PREFILL:
            case JOB_BOTH:
                size = data_byte * p["B"] * p["OC"] * stage.token_num /
                       (slice_y * slice_x) / 3;
                break;
            case JOB_DECODE:
                size =
                    data_byte * p["B"] * p["OC"] * 1 / (slice_y * slice_x) / 3;
                break;
            default:
                assert(false && "Unsupported job type");
            }

            cout << "[GPU MATMUL PD]: size: " << size << endl;

            char format_label_k[100];
            sprintf(format_label_k, "%s%s%sk#%d", prefix.c_str(), ETERNAL_PREFIX,
                    KVCACHE_PREFIX, stage.req_id);
            string label_k = format_label_k;

            char format_label_v[100];
            sprintf(format_label_v, "%s%s%sv#%d", prefix.c_str(), ETERNAL_PREFIX,
                    KVCACHE_PREFIX, stage.req_id);
            string label_v = format_label_v;

            prim_context->gpu_pos_locator_->updatePair(label_k, size);
            prim_context->gpu_pos_locator_->updatePair(label_v, size);

            AddrPosKey key_k, key_v;
            prim_context->gpu_pos_locator_->findPair(label_k, key_k);
            prim_context->gpu_pos_locator_->findPair(label_v, key_v);

            gpu_write_generic(context, key_k.pos + (key_k.size - size), size,
                              mem_time, false);
            gpu_write_generic(context, key_v.pos + (key_v.size - size), size,
                              mem_time, false);
        }


        prim_context->gpu_pos_locator_->updatePair(
            prim_context->datapass_label_->outdata,
            GetFromPairedVector(data_chunk, "output"));
        prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->outdata, out_key);

        cout << prim_context->cid
             << " [matmul_forward_gpu_pd] before write: " << mem_time
             << " at addr " << out_key.pos << endl;
        gpu_write_generic(context,
                          out_key.pos +
                              GetFromPairedVector(data_chunk, "output") *
                                  fetch_index,
                          GetFromPairedVector(data_chunk, "output"), mem_time);
        int cycle = 0;

        CoreHWConfig *core_config = GetCoreHWConfig(prim_context->cid);
        ExuConfig *exu = core_config->exu;
        SfuConfig *sfu = core_config->sfu;

        if (exu->type == MAC_Array)
            cycle +=
                (p["B"] * p["T"] * p["C"] * p["OC"] * 2 / (slice_x * slice_y)) /
                (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
        else
            assert(false && "Unsupported tile type");

        if (sfu->type == Linear)
            cycle += 0 / sfu->x_dims * CYCLE;
        else
            assert(false && "Unsupported tile type");


        if (mem_time > cycle) {
            // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
            overlap_time = 0;
            LOG_VERBOSE(1, prim_context->cid,
                        "Prim name:" << name << RED << " cycle: " << cycle
                                     << ", dram_time: " << mem_time << RESET);

            // std::cout << RED << "cycle: " << cycle << ", dram_time: " <<
            // dram_time
            //           << RESET << std::endl;

        } else {
            overlap_time = cycle - mem_time;
            LOG_VERBOSE(1, prim_context->cid,
                        "Prim name:" << name << GREEN << " cycle: " << cycle
                                     << ", dram_time: " << mem_time << RESET);
        }
    } else {
        int slice_total = slice_x * slice_y;
        // input 读入
        gpu_read_generic(context,
                         input_mem_offset +
                             input_size / slice_total * fetch_index,
                         input_size / slice_total, mem_time);
#if GPU_CACHE_DEBUG == 1

        LOG_VERBOSE(1, context.prim_context->cid,
                    " data_size_weight / slice_x "
                        << data_size_weight / slice_x);


#endif
        // weight 读入
        // LOG_VERBOSE(1, context.prim_context->cid," data_size_weight / slice_x
        // " << data_size_weight / slice_x);

        gpu_read_generic(
            context, w_key.pos + w_key.size / slice_total * fetch_index,
            GetFromPairedVector(data_chunk, "weight") / slice_total, mem_time);
        // assert(false && "Unsupported job type");
        // bias 读入
        gpu_read_generic(
            context, b_key.pos + b_key.size / slice_total * fetch_index,
            GetFromPairedVector(data_chunk, "bias") / slice_total, mem_time);

        for (auto stage : prim_context->batch_info_) {
            int size = 0;
            switch (p["job_type"]) {
            case JOB_PREFILL:
            case JOB_BOTH:
                size = data_byte * p["B"] * p["OC"] * stage.token_num /
                       (slice_y * slice_x) / 3;
                break;
            case JOB_DECODE:
                size =
                    data_byte * p["B"] * p["OC"] * 1 / (slice_y * slice_x) / 3;
                break;
            default:
                assert(false && "Unsupported job type");
            }

            char format_label_k[1000];
            sprintf(format_label_k, "%s%s%sk#%d", prefix.c_str(),
                    ETERNAL_PREFIX, KVCACHE_PREFIX, stage.req_id);
            string label_k = format_label_k;

            char format_label_v[1000];
            sprintf(format_label_v, "%s%s%sv#%d", prefix.c_str(),
                    ETERNAL_PREFIX, KVCACHE_PREFIX, stage.req_id);
            string label_v = format_label_v;

            prim_context->gpu_pos_locator_->updatePair(label_k, size);
            prim_context->gpu_pos_locator_->updatePair(label_v, size);

            AddrPosKey key_k, key_v;
            prim_context->gpu_pos_locator_->findPair(label_k, key_k);
            prim_context->gpu_pos_locator_->findPair(label_v, key_v);

            // LOG_VERBOSE(1, context.prim_context->cid," matmul kv " << "
            // prefix "<< prefix
            // << " " << size << " key size " << key_k.size << " " << label_k);

            gpu_write_generic(context, key_k.pos + (key_k.size - size), size,
                              mem_time, false);
            gpu_write_generic(context, key_v.pos + (key_v.size - size), size,
                              mem_time, false);
        }


        prim_context->gpu_pos_locator_->updatePair(
            prim_context->datapass_label_->outdata,
            GetFromPairedVector(data_chunk, "output"));
        prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->outdata, out_key);

        cout << prim_context->cid
             << " [matmul_forward_gpu_pd] before write: " << mem_time
             << " at addr " << out_key.pos << endl;
        gpu_write_generic(context,
                          out_key.pos +
                              GetFromPairedVector(data_chunk, "output") *
                                  fetch_index,
                          GetFromPairedVector(data_chunk, "output"), mem_time);

        int cycle = 0;

        CoreHWConfig *core_config = GetCoreHWConfig(prim_context->cid);
        ExuConfig *exu = core_config->exu;
        SfuConfig *sfu = core_config->sfu;

        if (exu->type == MAC_Array)
            cycle +=
                (p["B"] * p["T"] * p["C"] * p["OC"] * 2 / (slice_x * slice_y)) /
                (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
        else
            assert(false && "Unsupported tile type");

        if (sfu->type == Linear)
            cycle += 0 / sfu->x_dims * CYCLE;
        else
            assert(false && "Unsupported tile type");


        if (mem_time > cycle) {
            // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
            overlap_time = 0;
            LOG_VERBOSE(1, prim_context->cid,
                        "Prim name:" << name << RED << " cycle: " << cycle
                                     << ", dram_time: " << mem_time << RESET);

            // std::cout << RED << "cycle: " << cycle << ", dram_time: " <<
            // dram_time
            //           << RESET << std::endl;

        } else {
            overlap_time = cycle - mem_time;
            LOG_VERBOSE(1, prim_context->cid,
                        "Prim name:" << name << GREEN << " cycle: " << cycle
                                     << ", dram_time: " << mem_time << RESET);
        }
        // cout << "B: " << B << ", T: " << T << ", C: " << C << ", OC: " << OC
        // << endl; assert(false);
    }
#endif

    cout << prim_context->cid
         << " [matmul_forward_gpu_pd] after write: " << mem_time << " at addr "
         << out_key.pos << endl;

    p["B"] /= gpu_B;

    return overlap_time;
}

GpuBase *matmul_forward_gpu_pd::clone() { return new matmul_forward_gpu_pd(*this); }