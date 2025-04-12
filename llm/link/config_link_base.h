#pragma once
#include "nlohmann/json.hpp"
#include "systemc.h"

#include "../monitor/config_helper_base.h"
#include "config_node_base.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

// config_link_base : the base class for all link configurations

using json = nlohmann::json;

enum LINK_TYPE {
    NAIVE,
    ACC_LINK,
};

class LinkConfig : public sc_module {
public:
    LINK_TYPE type;
    LinkConfig() : type(NAIVE) {}
    LinkConfig(LINK_TYPE t) : type(t) {}
    void print_self();
};

void LinkConfig::print_self() { cout << "Link type: " << type << endl; }

// class xxlink : public sc_module {
// public:
//     // SC_CTOR(xxlink) {
//     //     SC_THREAD(main_thread);
//     // }

//     // void main_thread();
// };