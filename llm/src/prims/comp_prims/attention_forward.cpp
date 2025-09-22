#include "systemc.h"

#include "prims/base.h"
#include "prims/comp_prims.h"
#include "utils/config_utils.h"
#include "utils/memory_utils.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

REGISTER_PRIM(Attention_f);

void Attention_f::initialize() {
    auto &p = param_value;
    data_size_input = {p["B"] * p["T"] * p["C"]};
    data_chunk = {{"preatt", p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"att", p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"output", p["B"] * p["T"] * p["C"] / (1 + 2 / p["R"])}};
}

void Attention_f::taskCore(TaskCoreContext &context, string prim_name,
                          u_int64_t dram_time, u_int64_t &exu_ops,
                          u_int64_t &sfu_ops) {
    // 写入preatt中间结果
    int temp_sram_addr = 0;
    int temp_sram_addr_prior = 0;
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context,
                         data_byte * GetFromPairedVector(data_chunk, "preatt"),
                         temp_sram_addr, dram_time);
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;

    // 读出preatt，计算自然指数，写入att
    sram_read_generic_temp(context, GetFromPairedVector(data_chunk, "preatt"),
                           temp_sram_addr_prior, dram_time);
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context, data_byte * GetFromPairedVector(data_chunk, "att"), temp_sram_addr,
                         dram_time);
    // 读出att
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_read_generic_temp(context, data_byte * GetFromPairedVector(data_chunk, "att"),
                           temp_sram_addr_prior, dram_time);

    auto &p = param_value;
    exu_ops = (uint64_t)p["B"] * p["NH"] * p["T"] * (p["T"] - 1) / 2 *
              (4 * p["C"] / p["NH"] + 5);
    sfu_ops = 0;
}