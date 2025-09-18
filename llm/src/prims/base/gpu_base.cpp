#include "prims/base.h"
#include "utils/system_utils.h"

void GpuBase::parse_compose(json j) {
    if (j.contains("grid_x")) {
        auto &data_gx = j["grid_x"];
        if (data_gx.is_number_integer())
            grid_x = data_gx;
        else
            grid_x = GetDefinedParam(j["grid_x"]);
    }

    if (j.contains("grid_y")) {
        auto &data_gy = j["grid_y"];
        if (data_gy.is_number_integer())
            grid_y = data_gy;
        else
            grid_y = GetDefinedParam(j["grid_y"]);
    }

    if (j.contains("block_x")) {
        auto &data_bx = j["block_x"];
        if (data_bx.is_number_integer())
            block_x = data_bx;
        else
            block_x = GetDefinedParam(j["block_x"]);
    }

    if (j.contains("block_y")) {
        auto &data_by = j["block_y"];
        if (data_by.is_number_integer())
            block_y = data_by;
        else
            block_y = GetDefinedParam(j["block_y"]);
    }

    auto &data_req_sm = j["require_sm"];
    if (data_req_sm.is_number_integer())
        req_sm = data_req_sm;
    else
        req_sm = GetDefinedParam(j["require_sm"]);
}

void GpuBase::parse_addr_label(json j) {
    string in_label = j["indata"];
    datapass_label.outdata = j["outdata"];

    std::vector<std::string> in_labels;

    std::istringstream iss(in_label);
    std::string word;

    // 保证DRAM_LABEL后面跟着另一个label
    while (iss >> word) {
        in_labels.push_back(word);
    }

    for (int i = 0; i < in_labels.size(); i++) {
        datapass_label.indata[i] = in_labels[i];
    }
}