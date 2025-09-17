#include "systemc.h"

#include "memory/dram/Dcachecore.h"
#include "prims/comp_base.h"
#include "prims/comp_prims.h"
#include "utils/print_utils.h"

void Split_conv::initialize() {
    // W = p->W, H = p->H, C = p->C, B = p->B;
    // pX = p->pX, pY = p->pY;
    // S = p->sY, K = p->kY;

    // inp_size = W * H * C * B;
    // input_size = W * H * C * B;

    // int oH = (H + 2 * pY - K) / S + 1;
    // int pH = oH / slice;
    // new_H = (pH - 1) * S + K;

    // out_size = B * C * new_H * (W + 2 * pX) * slice;

    ARGUS_EXIT("Split_conv not implemented.\n");
}

int Split_conv::taskCore(TaskCoreContext &context, string prim_name,
                         u_int64_t dram_time, u_int64_t &exu_ops,
                         u_int64_t &sfu_ops) {
    ARGUS_EXIT("Split_conv not implemented.\n");
}