#pragma once

#include "config_chip.h"
#include "nlohmann/json.hpp"
#include "systemc.h"

#include <vector>

using json = nlohmann::json;

// 用于统一函数接口
class BaseConfig {
public:
    virtual ~BaseConfig() {};
};
