#pragma once

#include <optional>

#include "nlohmann/json.hpp"
#include "systemc.h"

#include "config_base.h"
#include "monitor/config_helper_base.h"
#include "monitor/config_helper_core.h"

#include <vector>

class ChipConfig : public BaseConfig {
public:
    int id;
    vector<pair<int, int>> source_info;
    Config_helper_core *chip;
    void print_self();
    // virtual ~ChipConfig(){};
};

void ChipConfig::print_self() { chip->print_self(); }

// 读取json文件
void from_json(const json &j, ChipConfig &c) {}