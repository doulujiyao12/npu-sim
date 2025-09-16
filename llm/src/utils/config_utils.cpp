#include <regex>

#include "common/config.h"
#include "utils/config_utils.h"
#include "utils/print_utils.h"

int GetDefinedParam(string var) {
    for (auto v : vtable) {
        if (v.first == var)
            return v.second;
    }

    ARGUS_EXIT("Undefined variable ", var, ".\n");
    return;
}

template <typename T> void SetParamFromJson(json j, string field, T *target) {
    if (j.contains(field)) {
        json value = j[field];

        if (value.is_number_integer()) {
            *target = value;
            return;
        }

        for (auto v : vtable) {
            if (v.first == value) {
                *target = v.second;
                return;
            }
        }

        ARGUS_EXIT("Undefined variable ", value, ".\n");
    } else
        ARGUS_EXIT("Undefined field ", field, " in json.\n");
}

template <typename T>
void SetParamFromJson(json j, string field, T *target, T default_value) {
    if (j.contains(field)) {
        json value = j[field];

        if (value.is_number_integer()) {
            *target = value;
            return;
        }

        for (auto v : vtable) {
            if (v.first == value) {
                *target = v.second;
                return;
            }
        }

        *target = default_value;
    } else
        *target = default_value;
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

void ParseWorkloadConfig(json j) {
    if (j.contains("x"))
        GRID_X = j["x"];
    else
        GRID_X = 4;

    if (j.contains("comm_acc"))
        CORE_ACC_PAYLOAD = j["comm_acc"];
    else
        CORE_ACC_PAYLOAD = 1;

    if (j.contains("sram_size"))
        MAX_SRAM_SIZE = j["sram_size"];

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
                i, CoreHWConfig(i, exu, sfu, sample.dram_config, sample.dram_bw,
                                sample.sram_bitwidth)));
        }

        ExuConfig *exu = new ExuConfig(MAC_Array, c.exu->x_dims, c.exu->y_dims);
        SfuConfig *sfu = new SfuConfig(Linear, c.sfu->x_dims);
        g_core_hw_config.push_back(make_pair(c.id, c));

        sample = c;
    }

    for (int i = sample.id + 1; i < GRID_SIZE; i++) {
        ExuConfig *exu =
            new ExuConfig(MAC_Array, sample.exu->x_dims, sample.exu->y_dims);
        SfuConfig *sfu = new SfuConfig(Linear, sample.sfu->x_dims);
        g_core_hw_config.push_back(
            make_pair(i, CoreHWConfig(i, exu, sfu, sample.dram_config,
                                      sample.dram_bw, sample.sram_bitwidth)));
    }
}