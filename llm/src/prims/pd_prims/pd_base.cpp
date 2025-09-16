#include "prims/pd_base.h"
#include "utils/system_utils.h"

#include <sstream>

void pd_base::parseAddress(json j) {
    if (j.contains("input")) {
        const auto &inputVal = j["input"];
        if (inputVal.is_number_integer())
            inp_offset = inputVal;
        else
            inp_offset = GetDefinedParam(j["input"]);
    } else
        inp_offset = 0;

    if (j.contains("data")) {
        const auto &dataVal = j["data"];
        if (dataVal.is_number_integer())
            data_offset = dataVal;
        else
            data_offset = GetDefinedParam(j["data"]);
    } else
        data_offset = 0;

    if (j.contains("out")) {
        const auto &outputVal = j["out"];
        if (outputVal.is_number_integer())
            out_offset = outputVal;
        else
            out_offset = GetDefinedParam(j["out"]);
    } else
        out_offset = 0;
}

void pd_base::parseSramLabel(json j) {
    string in_label = j["indata"];
    datapass_label.outdata = j["outdata"];

    std::vector<std::string> in_labels;

    std::istringstream iss(in_label);
    std::string word;
    std::string temp;

    // 保证DRAM_LABEL后面跟着另一个label
    while (iss >> word) {
        if (word == DRAM_LABEL) {
            temp = word;
            if (iss >> word) {
                temp += " " + word;
            }
            in_labels.push_back(temp);
        } else {
            in_labels.push_back(word);
        }
    }

    for (int i = 0; i < in_labels.size(); i++) {
        datapass_label.indata[i] = in_labels[i];
    }
}