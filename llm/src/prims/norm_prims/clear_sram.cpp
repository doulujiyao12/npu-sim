#include "systemc.h"
#include <string>

#include "common/memory.h"
#include "prims/norm_prims.h"
#include "prims/base.h"
#include "utils/system_utils.h"
#include "utils/prim_utils.h"

REGISTER_PRIM(Clear_sram);

void Clear_sram::printSelf() {
    cout << "<clear_sram>\n";
}

void Clear_sram::deserialize(sc_bv<128> buffer) {}

sc_bv<128> Clear_sram::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));

    return d;
}

int Clear_sram::taskCoreDefault(TaskCoreContext &context) {
    cout << "[INFO] before clear_sram: sram_addr=" << *(context.sram_addr)
         << endl;
#if USE_SRAM_MANAGER == 0
    vector<pair<string, AddrPosKey>> temp_list;
    // sram_pos_locator->printAllKeys();
    for (auto record : prim_context->sram_pos_locator_->data_map) {
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

    prim_context->sram_pos_locator_->clearAll();
    int pos = 0;
    for (auto record : temp_list) {
        auto size = record.second.size;
        int dma_read_count = size * 8 / (GetCoreHWConfig(prim_context->cid).sram_bitwidth * SRAM_BANKS);
        int byte_residue =
            size * 8 - dma_read_count * (GetCoreHWConfig(prim_context->cid).sram_bitwidth * SRAM_BANKS);
        int single_read_count =
            CeilingDivision(byte_residue, GetCoreHWConfig(prim_context->cid).sram_bitwidth);

        AddrPosKey temp_key = AddrPosKey(pos, size);
        u_int64_t temp_addr = 0;
        prim_context->sram_pos_locator_->addPair(record.first, temp_key, context, temp_addr);
        cout << "\tAdd label <" << record.first << "> at offset " << pos
             << endl;

        pos += dma_read_count * SRAM_BANKS + single_read_count;
    }

    *(context.sram_addr) = pos;
    cout << "[INFO] after clear_sram: sram_addr=" << pos << endl;
#endif
    prim_context->loop_cnt += 1;


    // CTODO: GC time count
    return 0;
}