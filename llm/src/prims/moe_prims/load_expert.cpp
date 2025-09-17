#include "prims/moe_prims.h"
#include "utils/memory_utils.h"
#include "utils/system_utils.h"

void load_expert::print_self(string prefix) {
    cout << prefix << "<load_expert>" << endl;
    cout << prefix << "\tE_N" << E_N << ", strategy: " << strategy << endl;
}

void load_expert::initialize() {}

void load_expert::parseJson(json j) {
    E_N = GetDefinedParam(j["E_N"]);
    K = GetDefinedParam(j["K"]);

    if (j.contains("strategy")) {
        string str_strategy = j["strategy"];
        if (str_strategy == "none")
            strategy = MOE_LOAD_STRATEGY_NONE;
        else if (str_strategy == "hot")
            strategy = MOE_LOAD_STRATEGY_HOT;
        else if (str_strategy == "random")
            strategy = MOE_LOAD_STRATEGY_RANDOM;
        else
            strategy = MOE_LOAD_STRATEGY_BEST;
    } else
        strategy = MOE_LOAD_STRATEGY_NONE;

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);
}

int load_expert::sram_utilization(DATATYPE datatype, int cid) {
    int total_sram = 0;

    return total_sram;
}

void load_expert::deserialize(sc_bv<128> buffer) {
    E_N = buffer.range(23, 8).to_uint64();
    K = buffer.range(39, 24).to_uint64();
    datatype = (DATATYPE)buffer.range(41, 40).to_uint64();
    OC = buffer.range(57, 42).to_uint64();
    C = buffer.range(73, 58).to_uint64();

    initialize();
}

sc_bv<128> load_expert::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(LOAD_EXPERT_TYPE);
    d.range(23, 8) = sc_bv<16>(E_N);
    d.range(39, 24) = sc_bv<16>(K);
    d.range(41, 40) = sc_bv<2>(datatype);
    d.range(57, 42) = sc_bv<16>(OC);
    d.range(63, 58) = sc_bv<6>(C);

    return d;
}

int load_expert::taskCoreDefault(TaskCoreContext &context) {
    // 所用时间
    u_int64_t dram_time = 0;
    u_int64_t overlap_time = 0;

    // 数据维度
    int data_size_weight_single = OC * C;
    int data_size_bias_single = OC;

    // dram地址
    u_int64_t dram_addr_tile = 0; //cid * dataset_words_per_tile;

    // 获取单个专家的所有数据
    auto label_weight_prefix_1 = datapass_label.indata[0] + "_w_";
    auto label_weight_prefix_2 = datapass_label.indata[1] + "_w_";
    auto label_weight_prefix_3 = datapass_label.indata[2] + "_w_";
    auto label_bias_prefix_1 = datapass_label.indata[0] + "_b_";
    auto label_bias_prefix_2 = datapass_label.indata[1] + "_b_";
    auto label_bias_prefix_3 = datapass_label.indata[2] + "_b_";
    int exp_1;

    if (strategy == MOE_LOAD_STRATEGY_NONE) {
        cout << "[Load expert]: No load expert\n";
        return 0;
    }

    if (strategy == MOE_LOAD_STRATEGY_HOT) {
        exp_1 = 0;
        int max_cnt = -1;
        for (int i = 0; i < selected_experts->size(); i++) {
            int cnt = (*selected_freq)[i];
            if (cnt > max_cnt) {
                max_cnt = cnt;
                exp_1 = i;
            }
        }
    }

    else if (strategy == MOE_LOAD_STRATEGY_RANDOM) {
        exp_1 = rand() % E_N;
    }

    prefetched_experts->clear();
    prefetched_experts->push_back(exp_1);

    // 将单个专家的所有数据load到sram中
    checkStaticData(context, dram_time, dram_addr_tile,
                      data_size_weight_single, label_weight_prefix_1 + to_string(exp_1));
    checkStaticData(context, dram_time, dram_addr_tile, data_size_bias_single,
                      label_bias_prefix_1 + to_string(exp_1));
    checkStaticData(context, dram_time, dram_addr_tile,
                      data_size_weight_single, label_weight_prefix_2 + to_string(exp_1));
    checkStaticData(context, dram_time, dram_addr_tile, data_size_bias_single,
                      label_bias_prefix_2 + to_string(exp_1));
    checkStaticData(context, dram_time, dram_addr_tile,
                      data_size_weight_single, label_weight_prefix_3 + to_string(exp_1));
    checkStaticData(context, dram_time, dram_addr_tile, data_size_bias_single,
                      label_bias_prefix_3 + to_string(exp_1));

    cout << "[load_expert] Prefetch expert: " << exp_1 << endl;
    return 0;
}

int load_expert::task() { return 0; }