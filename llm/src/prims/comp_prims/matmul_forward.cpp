#include "systemc.h"
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>

#include "common/system.h"
#include "defs/global.h"
#include "memory/dram/Dcachecore.h"
#include "prims/base.h"
#include "prims/comp_prims.h"
#include "utils/memory_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

REGISTER_PRIM(Matmul_f);

void Matmul_f::initialize() {
    auto &p = param_value;
    data_size_input = {p["B"] * p["T"] * p["C"]};
    data_chunk = {{"weight", p["C"] * p["OC"]},
                  {"bias", p["OC"]},
                  {"output", p["B"] * p["T"] * p["OC"]}};
}

void Matmul_f::taskCore(TaskCoreContext &context, string prim_name,
                        u_int64_t &dram_time, u_int64_t &exu_ops,
                        u_int64_t &sfu_ops) {
    cout << "Core " << prim_context->cid << " Matmul_f\n";
    ARGUS_PRINT(dram_time);

    auto label_weight = ETERNAL_PREFIX + prim_name + "_w";
    checkStaticData(context, dram_time, data_chunk_addr["weight"],
                    GetFromPairedVector(data_chunk, "weight"), label_weight,
                    false);

    auto label_bias = ETERNAL_PREFIX + prim_name + "_b";
    checkStaticData(context, dram_time, data_chunk_addr["bias"],
                    GetFromPairedVector(data_chunk, "bias"), label_bias, false);
    cout << "Core " << prim_context->cid << " Matmul_f\n";
    ARGUS_PRINT(dram_time);

    auto &p = param_value;
#if PERFORMANCE_MODE == 1

    ExuConfig *exu = GetCoreHWConfig(context.cid)->exu;

    uint64_t weight_tile_x = (p["C"] + exu->x_dims - 1) / exu->x_dims;
    uint64_t weight_tile_y = (p["OC"] + exu->y_dims - 1) / exu->y_dims;

    uint64_t padding_input_x =
        (p["T"] * p["B"]) > exu->x_dims ? p["T"] * p["B"] : exu->x_dims;

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
                cout << "[MATMUL] Core " << prim_context->cid
                     << ": Checking input "
                     << prim_context->datapass_label_->indata[p] << "..."
                     << endl;
                prefReadData(context, dram_time, data_size_input[p],
                             prim_context->datapass_label_->indata[p]);
            }
        }
    }

    exu_ops = performance_cycle;
    sfu_ops = 0;
#else
    // 计算overlap并写回output数据
    // cout << "matmul output data size: " << data_size_out << endl;
    exu_ops = (uint64_t)p["B"] * p["OC"] * p["T"] * p["C"] * 2;
    sfu_ops = 0;
#endif
}