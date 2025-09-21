#include "prims/gpu_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void Gelu_f_gpu::initialize() {
    auto &p = param_value;
    input_size = {data_byte * p["N"]};
    data_chunk = {{"output", data_byte * p["N"] / (slice_x * slice_y)}};
}


int Gelu_f_gpu::taskCoreDefault(TaskCoreContext &context) {
    auto &p = param_value;
    p["N"] *= gpu_B;

    int mem_time = 0;
    auto input_mem_offset = 0;
    if (!prim_context->gpu_pos_locator_->findPair(
            prim_context->datapass_label_->indata[0], input_mem_offset)) {
        printf("[ERROR] Gelu_f_gpu: prim_context->gpu_pos_locator_ cannot find "
               "the label: "
               "%s\n",
               prim_context->datapass_label_->indata[0].c_str());
        sc_stop();
    }

    int overlap_time = 0;
#if USE_L1L2_CACHE == 1
    gpu_read_generic(context,
                     input_mem_offset +
                         input_size / (slice_x * slice_y) * fetch_index,
                     input_size / (slice_x * slice_y), mem_time);

    // overlap_time = mem_time;
    AddrPosKey out_key;
    prim_context->gpu_pos_locator_->updatePair(
        prim_context->datapass_label_->outdata,
        GetFromPairedVector(data_chunk, "output"));
    prim_context->gpu_pos_locator_->findPair(
        prim_context->datapass_label_->outdata, out_key);

    gpu_write_generic(context,
                      out_key.pos + GetFromPairedVector(data_chunk, "output") *
                                        fetch_index,
                      GetFromPairedVector(data_chunk, "output"), mem_time);
    int cycle = 0;
    int cid = context.cid;

    CoreHWConfig *core_config = GetCoreHWConfig(cid);
    ExuConfig *exu = core_config->exu;
    SfuConfig *sfu = core_config->sfu;

    if (exu->type == MAC_Array)
        cycle += 0 / (slice_x * slice_y) /
                 (exu->x_dims * exu->y_dims * 2 * comp_util) * CYCLE;
    else
        assert(false && "Unsupported tile type");

    if (sfu->type == Linear)
        cycle += p["N"] / (slice_x * slice_y) / sfu->x_dims * CYCLE;
    else
        assert(false && "Unsupported tile type");


    if (mem_time > cycle) {
        // 因为dram 已经wait 过了，所以额外的 overlap_time = 0
        overlap_time = 0;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << RED << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);

        // std::cout << RED << "cycle: " << cycle << ", dram_time: " <<
        // dram_time
        //           << RESET << std::endl;

    } else {
        overlap_time = cycle - mem_time;
        LOG_VERBOSE(1, context.cid,
                    "Prim name:" << name << GREEN << " cycle: " << cycle
                                 << ", dram_time: " << mem_time << RESET);
    }
#endif

    cout << "[Gelu_f_gpu] after write: " << overlap_time << endl;

    p["N"] /= gpu_B;

    return overlap_time;
}

GpuBase *Gelu_f_gpu::clone() { return new Gelu_f_gpu(*this); }