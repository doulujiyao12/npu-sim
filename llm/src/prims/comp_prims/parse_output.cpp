#include "prims/comp_prims.h"
#include "utils/system_utils.h"

void parse_output::print_self(string prefix) {
    cout << prefix << "<parse_output>" << endl;
    cout << prefix << "size: " << size << endl;
}

void parse_output::initialize() {
    out_size = size;
    input_size = size;
    inp_size = size;

    if (datatype == INT8)
        data_byte = 1;
    else if (datatype == FP16)
        data_byte = 2;
}

void parse_output::parseJson(json j) {
    size = GetDefinedParam(j["size"]);

    initialize();

    if (j.contains("dram_address"))
        parseAddress(j["dram_address"]);

    if (j.contains("sram_address"))
        parseSramLabel(j["sram_address"]);
}

int parse_output::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void parse_output::deserialize(sc_bv<128> buffer) {
    size = buffer.range(39, 8).to_uint64();

    initialize();
}

sc_bv<128> parse_output::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PARSE_OUTPUT_TYPE);
    d.range(39, 8) = sc_bv<32>(size);

    return d;
}

int parse_output::task_core(TaskCoreContext &context) {
    // do nothing

    return 0;
}

int parse_output::task() { return 0; }