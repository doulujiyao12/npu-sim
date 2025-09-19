#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void matmul_forward_gpu_pd::initialize() {
    auto &p = param_value;
    input_size = {data_byte * p["B"] * p["T"] * p["C"]};
    data_chunk = {{"weight", data_byte * p["C"] * p["OC"]},
                  {"bias", data_byte * p["C"]},
                  {"output", data_byte * p["B"] * p["T"] * p["oC"] /
                                 (slice_x * slice_y)}};
}

int Matmul_f_gpu::taskCoreDefault(TaskCoreContext &context) {
    auto &p = param_value;
    p["B"] *= gpu_B;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->indata[0], input_mem_offset)) {
        printf("[ERROR] Matmul_f_gpu: prim_context->gpu_pos_locator_ cannot "
               "find the label: "
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

    auto label_weight = prefix + "_w";
    AddrPosKey w_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "weight"));
    prim_context->gpu_pos_locator_->fetchPair(label_weight, w_key);

    auto label_bias = prefix + "_b";
    AddrPosKey b_key = AddrPosKey(0, GetFromPairedVector(data_chunk, "bias"));
    prim_context->gpu_pos_locator_->fetchPair(label_bias, b_key);

    cout << prim_context->cid << " [Matmul_f_gpu] before read1: " << mem_time
         << " at addr " << input_mem_offset << endl;

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    if (gpu_inner == true) {
        // 通过fetch_index计算位置
        int row_index = fetch_index / slice_x;
        int col_index = fetch_index % slice_x;

        // input 读入
        gpu_read_generic(context,
                         input_mem_offset + input_size / slice_y * row_index,
                         input_size / slice_y, mem_time);

        // weight 读入
        gpu_read_generic(context, w_key.pos + w_key.size / slice_x * col_index,
                         GetFromPairedVector(data_chunk, "weight") / slice_x,
                         mem_time);

        // bias 读入
        gpu_read_generic(context, b_key.pos + b_key.size / slice_x * col_index,
                         GetFromPairedVector(data_chunk, "bias") / slice_x,
                         mem_time);

        // TODO: 模拟计算cycle数
        // overlap_time = mem_time;
        AddrPosKey out_key;
        prim_context->gpu_pos_locator_->updatePair(
            prim_context->datapass_label_->outdata,
            GetFromPairedVector(data_chunk, "output") * (slice_x * slice_y));
        prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->outdata, out_key);
        cout << prim_context->cid
             << " [Matmul_f_gpu] before write: " << mem_time << " at addr "
             << out_key.pos << endl;
        gpu_write_generic(context,
                          out_key.pos +
                              GetFromPairedVector(data_chunk, "output") *
                                  fetch_index,
                          GetFromPairedVector(data_chunk, "output"), mem_time);

        int cycle = 0;

        CoreHWConfig core_config = GetCoreHWConfig(prim_context->cid);
        ExuConfig *exu = core_config.exu;
        SfuConfig *sfu = core_config.sfu;

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
        gpu_read_generic(
            context, input_mem_offset + input_size / slice_total * fetch_index,
            input_size / slice_total, mem_time);

        // weight 读入
        gpu_read_generic(
            context, w_key.pos + w_key.size / slice_total * fetch_index,
            GetFromPairedVector(data_chunk, "weight") / slice_total, mem_time);

        // bias 读入
        gpu_read_generic(
            context, b_key.pos + b_key.size / slice_total * fetch_index,
            GetFromPairedVector(data_chunk, "bias") / slice_total, mem_time);

        AddrPosKey out_key;
        prim_context->gpu_pos_locator_->updatePair(
            prim_context->datapass_label_->outdata,
            GetFromPairedVector(data_chunk, "output") * slice_total);
        prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->outdata, out_key);
        cout << prim_context->cid
             << " [Matmul_f_gpu] before write: " << mem_time << " at addr "
             << out_key.pos << endl;
        gpu_write_generic(
            context, out_key.pos,
            GetFromPairedVector(data_chunk, "output") * slice_total, mem_time);

        int cycle = 0;

        CoreHWConfig core_config = GetCoreHWConfig(prim_context->cid);
        ExuConfig *exu = core_config.exu;
        SfuConfig *sfu = core_config.sfu;

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
    }
#endif

    cout << prim_context->cid << " [Matmul_f_gpu] after write: " << overlap_time
         << endl;

    p["B"] /= gpu_B;

    return overlap_time;
}

GpuBase *Matmul_f_gpu::clone() { return new Matmul_f_gpu(*this); }