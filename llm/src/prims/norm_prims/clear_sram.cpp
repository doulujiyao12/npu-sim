#include "systemc.h"
#include <string>

#include "common/memory.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "utils/system_utils.h"

void Clear_sram::print_self(string prefix) { cout << prefix << "<clear_sram>\n"; }

void Clear_sram::deserialize(sc_bv<128> buffer) {}

sc_bv<128> Clear_sram::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(0xd2);

    return d;
}

int Clear_sram::sram_utilization(DATATYPE datatype) { return 0; }

int Clear_sram::task_core(TaskCoreContext &context) {
    // CTODO: rearrange sram (need sram_pos_locator pointer)
    cout << "[INFO] before clear_sram: sram_addr=" << *(context.sram_addr) << endl;

    vector<pair<string, AddrPosKey>> temp_list;
    for (auto record : sram_pos_locator->data_map) {
        cout << "\tReading label <" << record.first << ">\n";

        if (!record.second.valid)
            continue;

        bool flag = true;
        string eternal = ETERNAL_PREFIX;
        for (int i = 0; i < eternal.length(); i++) {
            if (eternal[i] != record.first[i]) {
                flag = false;
                break;
            }
        }

        if (flag) {
            cout << "\t\tRetain.\n";
            temp_list.push_back(record);
        }
    }

    sram_pos_locator->clearAll();
    int pos = 0;
    for (auto record : temp_list) {
        auto size = record.second.size;
        int dma_read_count = size * 8 / (int)(SRAM_BITWIDTH * SRAM_BANKS);
        int byte_residue = size * 8 - dma_read_count * (SRAM_BITWIDTH * SRAM_BANKS);
        int single_read_count = ceiling_division(byte_residue, SRAM_BITWIDTH);

        AddrPosKey temp_key = AddrPosKey(pos, size);
        u_int64_t temp_addr = 0;
        sram_pos_locator->addPair(record.first, temp_key, context, temp_addr);
        cout << "\tAdd label <" << record.first << "> at offset " << pos << endl;

        pos += dma_read_count * SRAM_BANKS + single_read_count;
    }

    *(context.sram_addr) = pos;
    *(loop_cnt) += 1;

    cout << "[INFO] after clear_sram: sram_addr=" << pos << endl;

    // CTODO: GC time count
    return 0;
}

int Clear_sram::task() { return 0; }
