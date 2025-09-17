#include "prims/comp_prims.h"
#include "utils/system_utils.h"

void parse_input::initialize() {
    auto &p = param_value;
    data_size_input = {p["size"]};
    data_chunk = {{"output", 0}};
}

int parse_input::taskCore(TaskCoreContext &context, string prim_name,
                          u_int64_t dram_time, u_int64_t &exu_ops,
                          u_int64_t &sfu_ops) {
    // 将input_label这个标签存储在指定label中
    string inp_label = INPUT_LABEL;
    sram_pos_locator->changePairName(inp_label, datapass_label.indata[0]);

    cout << "[PARSE_INPUT] Core " << cid << ": Changed " << inp_label << " to "
         << datapass_label.indata[0] << endl;

    exu_ops = 0;
    sfu_ops = 0;
}