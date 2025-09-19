#include "prims/base.h"
#include "utils/config_utils.h"
#include "utils/prim_utils.h"
#include "utils/print_utils.h"
#include "utils/system_utils.h"

sc_bv<128> GpuBase::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));
    d.range(8, 8) = sc_bv<1>(datatype);
    d.range(24, 9) = sc_bv<16>(fetch_index);
    d.range(32, 25) = sc_bv<8>(slice_x);
    d.range(40, 33) = sc_bv<8>(slice_y);
    d.range(56, 41) = sc_bv<16>(req_sm);

    // 所有参数平均分配
    if (param_name.size() > 10) {
        ARGUS_EXIT("Primitive with # params = ", param_name.size(),
                   " is not supported.\n");
        return d;
    }

    int pos = 57;
    if (param_name.size() <= 2) {
        for (auto &pair : param_value) {
            d.range(pos + 31, pos) = sc_bv<32>(pair.second);
            pos += 32;
        }
    } else if (param_name.size() <= 4) {
        for (auto &pair : param_value) {
            d.range(pos + 15, pos) = sc_bv<16>(pair.second);
            pos += 16;
        }
    } else {
        for (auto &pair : param_value) {
            d.range(pos + 7, pos) = sc_bv<8>(pair.second);
            pos += 8;
        }
    }
}

void GpuBase::deserialize(sc_bv<128> buffer) {
    datatype = DATATYPE(buffer.range(8, 8).to_uint64());
    fetch_index = buffer.range(24, 9).to_uint64();
    slice_x = buffer.range(32, 25).to_uint64();
    slice_y = buffer.range(40, 33).to_uint64();
    req_sm = buffer.range(56, 41).to_uint64();

    int pos = 5761;
    if (param_name.size() <= 2) {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 31, pos).to_uint64();
            pos += 32;
        }
    } else if (param_name.size() <= 4) {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 15, pos).to_uint64();
            pos += 16;
        }
    } else {
        for (auto &param : param_name) {
            param_value[param] = buffer.range(pos + 7, pos).to_uint64();
            pos += 8;
        }
    }

    initialize();
    initializeDefault();
}

void GpuBase::parseCompose(json j) {
    SetParamFromJson(j, "slice_x", &slice_x);
    SetParamFromJson(j, "slice_y", &slice_y);
    SetParamFromJson(j, "req_sm", &req_sm);
}

void GpuBase::parseJson(json j) {
    for (auto &param : param_name) {
        SetParamFromJson(j, param, &param_value[param]);
    }

    initialize();
    initializeDefault();

    if (j.contains("compose"))
        parseCompose(j["compose"]);
}

void GpuBase::initializeDefault() {
    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;

    input_size = 0;
    for (auto &input : data_size_input)
        input_size += input;

    out_size = -1;
    for (const auto &chunk : data_chunk) {
        if (chunk.first == "output") {
            out_size = chunk.second;
            break;
        }
    }

    if (out_size < 0)
        ARGUS_EXIT("No output chunk found.\n");
}

void GpuBase::printSelf() {
    cout << "<" + name + ">\n";

    for (auto &pair : param_value)
        cout << "\t" << pair.first << ": " << pair.second << endl;

    for (auto &pair : data_chunk)
        cout << "\t" << pair.first << ": " << pair.second << endl;
}