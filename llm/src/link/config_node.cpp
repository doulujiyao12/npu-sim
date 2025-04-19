#include "link/config_node.h"

#include "utils/system_utils.h"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

using json = nlohmann::json;

void NodeConfig::print_self() {
    for (auto chip : chips) {
        chip.print_self();
    }
} 

void from_json(const json &j, NodeConfig &c) {
    //TODO 
}