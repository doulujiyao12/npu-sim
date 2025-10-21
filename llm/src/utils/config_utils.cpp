#include <regex>

#include "common/config.h"
#include "defs/spec.h"
#include "utils/config_utils.h"
#include "utils/print_utils.h"

int GetDefinedParam(string var) {
    for (auto v : vtable) {
        if (v.first == var)
            return v.second;
    }

    ARGUS_EXIT("Undefined variable ", var, ".\n");
    return 1;
}

void ParseSimulationType(json j) {
    std::unordered_map<string, SIM_MODE> sim_mode_map = {
        {"dataflow", SIM_DATAFLOW},
        {"gpu", SIM_GPU},
        {"sched_pd", SIM_PD},
        {"sched_pds", SIM_PDS},
        {"gpu_pd", SIM_GPU_PD}};

    if (j.contains("mode")) {
        auto mode = j["mode"];

        if (sim_mode_map.find(mode) != sim_mode_map.end())
            SYSTEM_MODE = sim_mode_map[mode];
        else
            ARGUS_EXIT("Unsupported simulation mode ", mode);
    } else
        SYSTEM_MODE = SIM_DATAFLOW;

    cout << "Simulation mode: " << SYSTEM_MODE << "\n";

    if (SYSTEM_MODE == SIM_GPU) {
        if (j.contains("chips"))
            CORE_PER_SM = j["chips"][0]["core_per_sm"];
        if (USE_L1L2_CACHE != 1)
            ARGUS_EXIT("L1L2 cache unavailable for GPU simulation.\n");
    }
}

void ParseHardwareConfig(json j) {
    if (j.contains("x"))
        GRID_X = j["x"];

    if (j.contains("sram_size"))
        HW_SRAM_SIZE = j["sram_size"];
    if (j.contains("core_credit"))
        HW_CORE_CREDIT = j["core_credit"];
    if (j.contains("pd_ratio"))
        HW_PD_RATIO = j["pd_ratio"];
    if (j.contains("dram_bitwidth"))
        HW_DRAM_BITWIDTH = j["dram_bitwidth"];
    if (j.contains("noc_payload_per_cycle"))
        HW_NOC_PAYLOAD_PER_CYCLE = j["noc_payload_per_cycle"];

    GRID_Y = GRID_X;
    GRID_SIZE = GRID_X * GRID_Y;

    auto config_cores = j["cores"];
    CoreHWConfig sample = config_cores[0];

    for (auto core : config_cores) {
        CoreHWConfig c = core;
        for (int i = sample.id + 1; i < c.id; i++) {
            ExuConfig *exu = new ExuConfig(MAC_Array, sample.exu->x_dims,
                                           sample.exu->y_dims);
            SfuConfig *sfu = new SfuConfig(Linear, sample.sfu->x_dims);
            g_core_hw_config.push_back(make_pair(
                i, new CoreHWConfig(i, exu, sfu, sample.dram_config,
                                    sample.dram_bw, sample.sram_bitwidth)));
        }

        ExuConfig *exu = new ExuConfig(MAC_Array, c.exu->x_dims, c.exu->y_dims);
        SfuConfig *sfu = new SfuConfig(Linear, c.sfu->x_dims);
        g_core_hw_config.push_back(
            make_pair(c.id, new CoreHWConfig(c.id, exu, sfu, c.dram_config,
                                             c.dram_bw, c.sram_bitwidth)));

        sample = c;
        sample.exu = new ExuConfig(MAC_Array, c.exu->x_dims, c.exu->y_dims);
        sample.sfu = new SfuConfig(Linear, c.sfu->x_dims);
    }

    for (int i = sample.id + 1; i < GRID_SIZE; i++) {
        ExuConfig *exu =
            new ExuConfig(MAC_Array, sample.exu->x_dims, sample.exu->y_dims);
        SfuConfig *sfu = new SfuConfig(Linear, sample.sfu->x_dims);
        g_core_hw_config.push_back(make_pair(
            i, new CoreHWConfig(i, exu, sfu, sample.dram_config, sample.dram_bw,
                                sample.sram_bitwidth)));
    }

    for (auto core : g_core_hw_config)
        core.second->printSelf();
}

void ParseSimulationConfig(json j) {
    // 设置仿真相关参数
    if (j.contains("use_beha_noc"))
        SPEC_USE_BEHA_NOC = j["use_beha_noc"];
    if (j.contains("use_beha_sram"))
        SPEC_USE_BEHA_SRAM = j["use_beha_sram"];
    if (j.contains("use_beha_dram"))
        SPEC_USE_BEHA_DRAM = j["use_beha_dram"];
    if (j.contains("use_beha_gemm"))
        SPEC_USE_PERF_GEMM = j["use_perf_gemm"];
    if (j.contains("kvcache_spill"))
        SPEC_KVCACHE_SPILL = j["kvcache_spill"];
    if (j.contains("load_static_as_tile"))
        SPEC_LOAD_STATIC_AS_TILE = j["load_static_as_tile"];
    if (j.contains("ttf_file"))
        SPEC_TTF_FILE = j["ttf_file"];
    if (j.contains("use_dramsys"))
        SPEC_USE_DRAMSYS = j["use_dramsys"];
}