#include "prims/norm_prims.h"

int Set_batch::task() { return 0; }

int Set_batch::task_core(TaskCoreContext &context) {
    target->clear();
    for (auto stage : batchInfo) {
        target->push_back(stage);
    }

    return 0;
}

void Set_batch::print_self(string prefix) { cout << prefix << "<Set_batch>\n"; }

int Set_batch::sram_utilization(DATATYPE datatype) { return 0; }

void Set_batch::parse_json(json j) {}

void Set_batch::deserialize(sc_bv<128> buffer) {
    int batch_size = buffer.range(11, 8).to_uint64();
    int pos = 12;
    for (int i = 0; i < batch_size; i++) {
        Stage s = Stage(buffer.range(pos + 9, pos).to_uint64(),
                        PD_PHASE(buffer.range(pos + 11, pos + 10).to_uint64()),
                        buffer.range(pos + 21, pos + 12).to_uint64());
        batchInfo.push_back(s);
        pos += 22;
    }
}

sc_bv<128> Set_batch::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SET_BATCH_TYPE);
    d.range(11, 8) = sc_bv<4>(batchInfo.size());

    int pos = 12;
    for (int i = 0; i < batchInfo.size(); i++) {
        d.range(pos + 9, pos) = sc_bv<10>(batchInfo[i].req_id);
        d.range(pos + 11, pos + 10) = sc_bv<2>(batchInfo[i].type);
        d.range(pos + 21, pos + 12) = sc_bv<10>(batchInfo[i].token_num);
        pos += 22;
    }

    return d;
}