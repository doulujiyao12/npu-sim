#include "systemc.h"
#include <string>

#include "common/memory.h"
#include "prims/norm_prims.h"
#include "prims/prim_base.h"
#include "utils/system_utils.h"

void Clear_sram::print_self(string prefix) {
    cout << prefix << "<clear_sram>\n";
}

void Clear_sram::deserialize(sc_bv<128> buffer) {}

sc_bv<128> Clear_sram::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(CLEAR_SRAM_TYPE);

    return d;
}

int Clear_sram::sram_utilization(DATATYPE datatype, int cid) { return 0; }

int Clear_sram::task_core(TaskCoreContext &context) {
    // CTODO: rearrange sram (need sram_pos_locator pointer)
    cout << "[INFO] before clear_sram: sram_addr=" << *(context.sram_addr)
         << endl;
#if USE_SRAM_MANAGER == 0
    vector<pair<string, AddrPosKey>> temp_list;
    // sram_pos_locator->printAllKeys();
    for (auto record : sram_pos_locator->data_map) {
        cout << "\tReading label <" << record.first << ">\n";

        if (!record.second.valid)
            continue;

        bool flag = true;

        // clear output last layer in core and reuse input 
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
    // for (const auto& item : temp_list) {
    //     std::cout << "Key: " << item.first << ", ";
    //     std::cout << "AddrPosKey - pos: " << item.second.pos << ", size: " <<
    //     item.second.size << std::endl;
    // }

    sram_pos_locator->clearAll();
    int pos = 0;
    for (auto record : temp_list) {
        auto size = record.second.size;
        int dma_read_count = size * 8 / (GetCoreHWConfig(cid).sram_bitwidth * SRAM_BANKS);
        int byte_residue =
            size * 8 - dma_read_count * (GetCoreHWConfig(cid).sram_bitwidth * SRAM_BANKS);
        int single_read_count =
            CeilingDivision(byte_residue, GetCoreHWConfig(cid).sram_bitwidth);

        AddrPosKey temp_key = AddrPosKey(pos, size);
        u_int64_t temp_addr = 0;
        sram_pos_locator->addPair(record.first, temp_key, context, temp_addr);
        cout << "\tAdd label <" << record.first << "> at offset " << pos
             << endl;

        pos += dma_read_count * SRAM_BANKS + single_read_count;
    }

    *(context.sram_addr) = pos;
    cout << "[INFO] after clear_sram: sram_addr=" << pos << endl;
#endif
    *(loop_cnt) += 1;


    // CTODO: GC time count
    return 0;
}

int Clear_sram::task() { return 0; }
