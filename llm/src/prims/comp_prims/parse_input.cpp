#include "prims/comp_prims.h"
#include "utils/system_utils.h"

void parse_input::print_self(string prefix) {
    cout << prefix << "<parse_input>" << endl;
    cout << prefix << "size: " << size << endl;
}

void parse_input::initialize() {
    out_size = size;
    input_size = size;
    inp_size = size;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void parse_input::parseJson(json j) {
    size = GetDefinedParam(j["size"]);

    initialize();

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);
}

int parse_input::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void parse_input::deserialize(sc_bv<128> buffer) {
    size = buffer.range(39, 8).to_uint64();

    initialize();
}

sc_bv<128> parse_input::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PARSE_INPUT_TYPE);
    d.range(39, 8) = sc_bv<32>(size);

    return d;
}

int parse_input::task_core(TaskCoreContext &context) {
    // 将input_label这个标签存储在指定label中
    string inp_label = INPUT_LABEL;
    sram_pos_locator->changePairName(inp_label, datapass_label.indata[0]);

    cout << "[PARSE_INPUT] Core " << cid << ": Changed " << inp_label << " to "
         << datapass_label.indata[0] << endl;

    return 0;
}

int parse_input::task() { return 0; }