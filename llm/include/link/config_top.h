#pragma once

#include "nlohmann/json.hpp"
#include "systemc.h"
#include "link/config_base.h"

#include "config_chip.h"
#include "config_node.h"
#include <string>
#include <vector>

using json = nlohmann::json;
class ChipConfig;
class TopConfig : public BaseConfig{
public:
    // TopConfig(std::string filename);
    TopConfig(std::string filename, std::string font_ttf);

    std::string filename;
    std::string font_ttf;
    // NodeConfig* node;
    // std::vector<ChipConfig*> chip_;
    std::vector<BaseConfig*> component_;

    int seq_index;  
    int pipeline;
    bool sequential;
    std::vector<std::pair<int, int>> source_info; // 记录一些全局变量

    void print_self();
    Type getType() const override {
        return Type::TYPE_TOP;
    }
    
    ~TopConfig();
};