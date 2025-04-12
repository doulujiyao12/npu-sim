#pragma once

#include "nlohmann/json.hpp"
#include "systemc.h"

#include "config_chip.h"
#include "config_node.h"
#include <string>
#include <vector>

using json = nlohmann::json;

class TopConfig {
public:
    TopConfig(std::string filename);

    // NodeConfig* node;
    std::vector<ChipConfig> *chip;

    int seq_index;
    int pipeline;
    bool sequential;
    vector<pair<int, int>> source_info; // 记录一些全局变量
};

TopConfig::TopConfig(std::string filename) {
    std::cout << "Loading Top config from " << filename << std::endl;
    std::ifstream file(filename);
    json j;
    file >> j;

    auto config_vars = j["vars"];
    for (auto var : config_vars.items()) {
        vtable.push_back(make_pair(var.key(), var.value()));
    }

    auto config_source = j["source"];
    for (auto source : config_source) {
        if (source.contains("loop")) {
            int loop_cnt = find_var(source["loop"]);
            for (int i = 0; i < loop_cnt; i++) {
                source_info.push_back(make_pair(source["dest"], find_var(source["size"])));
            }
        } else {
            source_info.push_back(make_pair(source["dest"], find_var(source["size"])));
        }
    }

    if (j.contains("pipeline")) {
        j.at("pipeline").get_to(pipeline);
    } else {
        pipeline = 1;
    }

    if (j.contains("sequential")) {
        j.at("sequential").get_to(sequential);
    } else {
        sequential = false;
    }

    auto config_chips = j["chips"];
    for (int i = 0; i < config_chips.size(); i++) {
        // ChipConfig chip
        ChipConfig chip;
        // TODO 定义一些全局变量
        json j = config_chips[i];
        // from_json(j, chip);
    }

    // 没写完 TODO
}

void from_json(const json &j, TopConfig &c) {
    auto config_vars = j["vars"];
    for (auto var : config_vars.items()) {
        vtable.push_back(make_pair(var.key(), var.value()));
    }
}