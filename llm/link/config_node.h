#pragma once

#include "nlohmann/json.hpp"
#include "systemc.h"

#include "config_base.h"
#include "config_chip.h"
#include "monitor/config_helper_base.h"
#include "monitor/config_helper_core.h"

#include <vector>

class NodeConfig : public BaseConfig {
public:
    int id;
    std::vector<ChipConfig> chips;
    void print_self();
};

void NodeConfig::print_self() {
    for (auto chip : chips) {
        chip.print_self();
    }
}