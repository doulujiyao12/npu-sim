#include "prims/pd_prims.h"
#include "utils/memory_utils.h"

void attention_forward_pd::initialize() {
    auto &p = param_value;
    data_size_input = {p["B"] * p["T"] * p["C"]};
    data_chunk = {{"preatt", p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"att", p["B"] * p["NH"] * p["T"] * p["T"]},
                  {"output", p["B"] * p["T"] * p["NH"] * p["DH"]}};
}

int attention_forward_pd::taskCore(TaskCoreContext &context, string prim_name,
                                   u_int64_t dram_time, u_int64_t &exu_ops,
                                   u_int64_t &sfu_ops) {
    auto &p = param_value;
    int cur_tokens = 0;

    // 查找kvcache! 需要使用相应的kvcache label 读出KV
    // 根据batchInfo进行，逻辑和普通prefill和decode相同
    for (auto stage : prim_context->batch_info_) {
        int batch = stage.req_id;

        AddrPosKey kcache;
        char format_label_k[100];
        sprintf(format_label_k, "%s%s%sk#%d", prim_name.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, batch);
        string label_decode_k = format_label_k;
        // cout << "decode_k: " << label_decode_k << endl;


        int flag =
            prim_context->sram_pos_locator_->findPair(label_decode_k, kcache);
        if (flag == -1) {
            printf("[ERROR] attention_forward_pd: failed to find label %s, "
                   "exit.\n",
                   label_decode_k.c_str());
            sc_stop();
        } else if (flag > 0) {
#if USE_SRAM_MANAGER == 1
            std::cout << "[INFO] CompBase: sram_pos_locator find the "
                         "label: "
                      << label_decode_k << " with flag: " << flag << std::endl;
            sram_first_write_generic(context, flag, kcache.dram_addr, dram_time,
                                     nullptr, label_decode_k, true,
                                     prim_context->sram_pos_locator_);

#else
            // TODO: DUMMY dram addr
            sram_first_write_generic(context, flag, inp_offset, dram_time,
                                     nullptr);
            kcache.spill_size = 0;
            prim_context->sram_pos_locator_->addPair(label_decode_k, kcache,
                                                     context, dram_time);
#endif
        }


        AddrPosKey vcache;
        char format_label_v[100];
        sprintf(format_label_v, "%s%s%sv#%d", prim_name.c_str(), ETERNAL_PREFIX,
                KVCACHE_PREFIX, batch);
        string label_decode_v = format_label_v;
        // cout << "decode_v: " << label_decode_v << endl;

        flag =
            prim_context->sram_pos_locator_->findPair(label_decode_v, vcache);
        if (flag == -1) {
            printf("[ERROR] attention_forward_pd: failed to find label %s, "
                   "exit.\n",
                   label_decode_v.c_str());
            sc_stop();
        } else if (flag > 0) {
#if USE_SRAM_MANAGER == 1
            std::cout << "[INFO] CompBase: sram_pos_locator find the "
                         "label: "
                      << label_decode_v << " with flag: " << flag << std::endl;
            sram_first_write_generic(context, flag, vcache.dram_addr, dram_time,
                                     nullptr, label_decode_v, true,
                                     prim_context->sram_pos_locator_);

#else
            // TODO: DUMMY dram addr
            sram_first_write_generic(context, flag, inp_offset, dram_time,
                                     nullptr);
            vcache.spill_size = 0;
            prim_context->sram_pos_locator_->addPair(label_decode_v, vcache,
                                                     context, dram_time);
#endif
        }

        // assert(flag == 0 && "sram does not have enough space");
        // dahu ??
        int sram_offset = 0;
#if USE_SRAM_MANAGER == 1
        prim_context->sram_pos_locator_->printAllKeysWithAllocId();
        // Print allocation IDs for debugging
        std::cout << label_decode_k << " " << label_decode_v
                  << " Key Allocation ID: " << kcache.alloc_id << " "
                  << vcache.alloc_id << std::endl;

        sram_read_generic(context, kcache.size, sram_offset, dram_time,
                          kcache.alloc_id, true,
                          prim_context->sram_pos_locator_);
        sram_read_generic(context, vcache.size, sram_offset, dram_time,
                          vcache.alloc_id, true,
                          prim_context->sram_pos_locator_);
#else
        // 读出k,v
        sram_read_generic(context, kcache.size, kcache.pos, dram_time);
        sram_read_generic(context, vcache.size, vcache.pos, dram_time);
#endif

        cur_tokens = kcache.size / (p["B"] * p["C"] * data_byte);
    }

    // 写入preatt中间结果
    int temp_sram_addr = 0;
    int temp_sram_addr_prior = 0;
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context, data_byte * data_chunk_addr["preatt"],
                         temp_sram_addr, dram_time);
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;

    // 读出preatt，计算自然指数，写入att
    sram_read_generic_temp(context, data_byte * data_chunk_addr["preatt"],
                           temp_sram_addr_prior, dram_time);
    temp_sram_addr_prior = temp_sram_addr;
    std::cout << "attention_forward sram_write_back_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_write_back_temp(context, data_byte * data_chunk_addr["att"],
                         temp_sram_addr, dram_time);
    // 读出att
    std::cout << "attention_forward sram_read_generic_temp: temp_sram_addr: "
              << temp_sram_addr << std::endl;
    sram_read_generic_temp(context, data_byte * data_chunk_addr["att"],
                           temp_sram_addr_prior, dram_time);

    exu_ops = (u_int64_t)p["B"] * p["NH"] * p["T"] * (p["T"] - 1) / 2 *
              (4 * p["C"] / p["NH"] + 5);
    sfu_ops = 0;
}