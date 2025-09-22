#include "systemc.h"

#include "common/memory.h"
#include "defs/global.h"
#include "prims/base.h"
#include "prims/norm_prims.h"
#include "utils/prim_utils.h"
#include "utils/system_utils.h"

REGISTER_PRIM(Set_addr);

void Set_addr::printSelf() {
    cout << "<Set_addr>\n";
    cout << "\tSram_addr: " << sram_addr << endl;
}

void Set_addr::deserialize(sc_bv<128> buffer) {
    sram_addr = buffer.range(31, 8).to_uint64();
    datatype = (DATATYPE)buffer.range(33, 32).to_uint64();

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        datapass_label.indata[i] = g_addr_label_table.findRecord(
            buffer.range(offset + 11, offset).to_uint64());
        offset += 12;
    }
    datapass_label.outdata = g_addr_label_table.findRecord(
        buffer.range(offset + 11, offset).to_uint64());
}

sc_bv<128> Set_addr::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(PrimFactory::getInstance().getPrimId(name));
    d.range(31, 8) = sc_bv<24>(sram_addr);
    d.range(33, 32) = sc_bv<2>(datatype);

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        d.range(offset + 11, offset) = sc_bv<12>(g_addr_label_table.addRecord(
            prim_context->datapass_label_->indata[i]));
        offset += 12;
    }
    d.range(offset + 11, offset) = sc_bv<12>(
        g_addr_label_table.addRecord(prim_context->datapass_label_->outdata));

    return d;
}

int Set_addr::taskCoreDefault(TaskCoreContext &context) {
    //  将datapass_label的内容复制到target中
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        prim_context->datapass_label_->indata[i] = datapass_label.indata[i];
    }
    prim_context->datapass_label_->outdata = datapass_label.outdata;

    return 0;
}