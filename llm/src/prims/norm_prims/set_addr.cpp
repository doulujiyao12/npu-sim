#include "systemc.h"

#include "common/memory.h"
#include "defs/global.h"
#include "prims/comp_base.h"
#include "prims/norm_prims.h"
#include "utils/system_utils.h"

void Set_addr::print_self(string prefix) {
    cout << prefix << "<Set_addr>\n";
    cout << prefix << "\tSram_addr: " << sram_addr << endl;
}

int Set_addr::sram_utilization(DATATYPE datatype, int cid) { return 0; }

void Set_addr::parseJson(json j) { sram_addr = GetDefinedParam(j["sram_addr"]); }

void Set_addr::deserialize(sc_bv<128> buffer) {
    sram_addr = buffer.range(31, 8).to_uint64();
    datatype = (DATATYPE)buffer.range(33, 32).to_uint64();

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        datapass_label->indata[i] = g_addr_label_table.findRecord(
            buffer.range(offset + 11, offset).to_uint64());
        offset += 12;
    }
    datapass_label->outdata = g_addr_label_table.findRecord(
        buffer.range(offset + 11, offset).to_uint64());
}

sc_bv<128> Set_addr::serialize() {
    sc_bv<128> d;
    d.range(7, 0) = sc_bv<8>(SET_ADDR_TYPE);
    d.range(31, 8) = sc_bv<24>(sram_addr);
    d.range(33, 32) = sc_bv<2>(datatype);

    int offset = 34;
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        d.range(offset + 11, offset) =
            sc_bv<12>(g_addr_label_table.addRecord(datapass_label->indata[i]));
        offset += 12;
    }
    d.range(offset + 11, offset) =
        sc_bv<12>(g_addr_label_table.addRecord(datapass_label->outdata));

    return d;
}

int Set_addr::taskCoreDefault(TaskCoreContext &context) {
    // std::cout << "Set Sram333 " << sram_addr << " to " << context.sram_addr
    // << endl;
    //  CTODO: 取消注释
    //  *(context.sram_addr) = sram_addr;
    //  将datapass_label的内容复制到target中
    for (int i = 0; i < MAX_SPLIT_NUM; i++) {
        target->indata[i] = datapass_label->indata[i];
    }
    target->outdata = datapass_label->outdata;

    return 0;
}
int Set_addr::task() { return 0; }