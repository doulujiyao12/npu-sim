// #pragma once
// #include "nlohmann/json.hpp"
// #include "systemc.h"

// #include "../monitor/config_helper_base.h"
// #include <filesystem>
// #include <iostream>
// #include <map>
// #include <queue>
// #include <vector>

// // 先虚构一个global memory，从chip的global memory中读取数据，chip->global
// // memory，先有一次数据搬移

// using json = nlohmann::json;

// class ClusterConfig {
// public:
//     int id;
//     vector<CoreConfig> cores;
//     void print_self(bool print_core = true);
// };

// void ClusterConfig::print_self(bool print_core) {
//     cout << "[Cluster " << id << "]\n";
//     if (print_core) {
//         for (auto core : cores) {
//             core.print_self();
//         }
//     }
// }