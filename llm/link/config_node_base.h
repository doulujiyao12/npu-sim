#pragma once
#include "nlohmann/json.hpp"
#include "systemc.h"

#include "../monitor/config_helper_base.h"
#include "config_cluster_base.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

using json = nlohmann::json;

class NodeConfig {
public:
    int id;
    vector<ClusterConfig> clusters;
    void print_self(bool print_core = true);
};

void NodeConfig::print_self(bool print_core) {
    cout << "[Node " << id << "]\n";
    for (auto cluster : clusters) {
        cluster.print_self(print_core);
    }
}